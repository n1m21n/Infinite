#!/usr/bin/env bash
# Pre-commit hygiene driver for Infinite.
#
# Builds the app, then drives its built-in env-var-gated self-test harness
# (src/main.cpp, search `getenv("INFINITE_`) through the real compiled
# .app binary — real ImGui frames, real GL draws, real node graph, not a
# mock. Each test spawns a small fixture graph, runs it for N frames, and
# printf's a verdict line ending in "OK", containing "FAIL", or ending in
# "BUG". This script greps for the failure markers and reports pass/fail
# per check.
#
# Usage:
#   .claude/skills/run-infinite-hygiene/driver.sh              # full suite
#   .claude/skills/run-infinite-hygiene/driver.sh --skip-build  # reuse existing build/
#   .claude/skills/run-infinite-hygiene/driver.sh --shot-only   # just render + screenshot, no test suite
#
# Exit code: 0 if the build succeeded and every check passed, 1 otherwise.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

BUILD_DIR="build"
BIN="$BUILD_DIR/Infinite.app/Contents/MacOS/Infinite"
SKIP_BUILD=0
SHOT_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --skip-build) SKIP_BUILD=1 ;;
    --shot-only) SHOT_ONLY=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

FAIL_MARK='FAIL|BUG$'
PASS=0
FAIL=0
FAILED_NAMES=()

# name:exitAfter — chosen empirically (each verified in-session to print its
# full verdict by that frame; padded a few frames for safety). Grouped by
# what they exercise; see SKILL.md for the map.
TESTS=(
  "UNDOTEST:10"
  "PATCHTEST:30"
  "ROUNDTRIPTEST:35"
  "GROUPTEST:30"
  "COMMENTTEST:15"
  "HIDETEST:35"
  "SELECTTEST:35"
  "DISTRIBUTETEST:10"
  "PHASE4TEST:10"
  "MINIVIEWPORTTEST:12"
  "COLORTEST:13"
  "MACROTEST:30"
  "PALETTETEST:30"
  "BYPASSTEST:30"
  "GEOTEST:30"
  "MESHOPTEST:30"
  "TEXT3DTEST:30"
  "PATHOCEANTEST:35"
  "SHADOWTEST:35"
  "MATFRAMETEST:35"
  "MAPTEST:35"
  "PADPATHTEST:35"
  "BUGTEST:35"
  "FIXTEST:35"
  "3DTEST:35"
  "TRANSFORMSWEEPTEST:10"
  "MAPPINGSWEEPTEST:10"
  "REVISIONSWEEPTEST:10"
  "ENVTEST:14"
  "PHASEATEST:35"
  "PHASECTEST:35"
  "PHASEDTEST:35"
  "PHASEETEST:35"
  "PHASEFTEST:35"
  "WRAPTEST:35"
  "LIVETEST:35"
  "PHASE1TEST:35"
  "DELETECRASHTEST:8"
  "AUDIOGRAPHTEST:8"
  "DRAGTEST:35"
  "WTDRAGTEST:35"
  "AUDIOPARAMSWEEPTEST:1"
  "AUDIOTEARDOWNSWEEPTEST:10"
  "AUDIOLIFECYCLETEST:8"
  "AUDIORECOVERYTEST:8"
  "SAMPLERDRAGTEST:600"
  "MEDIADRAGTEST:600"
  "PLUGINDRAGTEST:600"
  # Headless (exits before glfwInit), so the frame budget is irrelevant to it -
  # it polls a real asynchronous AU instantiation on its own bounded timer.
  "PLUGINSCANTEST:1"
)

step() { printf '\n== %s ==\n' "$1"; }

# ---------------------------------------------------------------------------
step "Build"
if [ "$SKIP_BUILD" -eq 1 ]; then
  echo "skipped (--skip-build)"
elif [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
  if ! cmake --build "$BUILD_DIR" -j8 2>&1 | tee /tmp/infinite_build.log; then
    echo "BUILD FAILED — see /tmp/infinite_build.log"
    exit 1
  fi
else
  echo "no existing build/, configuring fresh"
  if ! cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug 2>&1 | tee /tmp/infinite_build.log; then
    echo "CONFIGURE FAILED — see /tmp/infinite_build.log"
    exit 1
  fi
  if ! cmake --build "$BUILD_DIR" -j8 2>&1 | tee -a /tmp/infinite_build.log; then
    echo "BUILD FAILED — see /tmp/infinite_build.log"
    exit 1
  fi
fi

if [ ! -x "$BIN" ]; then
  echo "binary not found at $BIN after build"
  exit 1
fi

# ---------------------------------------------------------------------------
step "Visual smoke (screenshot)"
SHOT="/tmp/infinite_hygiene_shot.png"
rm -f "$SHOT"
IMAGERESYNTH_SCREENSHOT="$SHOT" INFINITE_SHOWCASE=1 "$BIN" >/tmp/infinite_shot.log 2>&1
if [ -f "$SHOT" ]; then
  echo "wrote $SHOT — Read it to eyeball rendering (nodes, previews, chrome all draw correctly)"
else
  echo "SCREENSHOT FAILED — see /tmp/infinite_shot.log"
  FAIL=$((FAIL+1))
  FAILED_NAMES+=("SCREENSHOT")
fi

if [ "$SHOT_ONLY" -eq 1 ]; then
  echo
  echo "shot-only run — skipping test suite"
  exit 0
fi

# ---------------------------------------------------------------------------
step "Self-test suite (${#TESTS[@]} checks)"
for spec in "${TESTS[@]}"; do
  name="${spec%%:*}"
  frames="${spec##*:}"
  out="/tmp/infinite_test_${name}.log"
  env "INFINITE_${name}=1" INFINITE_EXITAFTER="$frames" "$BIN" >"$out" 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then
    echo "  [CRASH] $name — exited $rc, see $out"
    FAIL=$((FAIL+1)); FAILED_NAMES+=("$name (crash)")
    continue
  fi
  if grep -qE "$FAIL_MARK" "$out"; then
    echo "  [FAIL]  $name — see $out"
    grep -E "$FAIL_MARK" "$out" | sed 's/^/          /'
    FAIL=$((FAIL+1)); FAILED_NAMES+=("$name")
  else
    verdict=$(grep -E ' OK$' "$out" | tail -1)
    echo "  [pass]  $name  ${verdict:+— $verdict}"
    PASS=$((PASS+1))
  fi
done

# ---------------------------------------------------------------------------
step "Summary"
echo "passed: $PASS   failed: $FAIL"
if [ $FAIL -gt 0 ]; then
  echo "failing checks: ${FAILED_NAMES[*]}"
  echo
  echo "known baseline (not a regression unless it changes): PHASE1TEST is"
  echo "occasionally flaky on particle-system timing; rerun once before treating it"
  echo "as a regression. PHASEATEST currently fails on a pre-existing \"Smooth\" node-"
  echo "name collision unrelated to audio work — see the spawned task to fix it."
  echo "known baseline: AUDIOPARAMSWEEPTEST currently reports [FAIL] on one Dynamics"
  echo "param (sidechainExternal — every audio input slot a candidate has gets the"
  echo "identical drive-tone buffer aliased in, main and sidechain alike, so"
  echo "switching which pin the detector reads from can never change the signature),"
  echo "two Delay params (sync, rateDiv — both gate the base delay time itself, the"
  echo "very knob that would need to differ to observe a change, so unlike"
  echo "feedback/tone/pan/ducking they can't be gated behind a short-path"
  echo "prerequisite), rateDiv alone (not sync — see below) on Chorus, Flanger,"
  echo "Phaser, Stutter and Tremolo (identical structural reason, same MusicTime"
  echo "table — Tremolo's own sync/rateDiv/rate comment in EffectDefs.cpp already"
  echo "says as much, it was just missing from this baseline list), and three Reverb"
  echo "params (decay, damping, predelay — all only change what the FDN's 8 lines"
  echo "*write*, and a line's own read trails its write by ~600-1100 samples at the"
  echo "default size, longer than the sweep's post-alteration measurement window)."
  echo "All hand-confirmed correct — see EffectDefs.cpp's comments on each param and"
  echo ".claude/skills/audio-node-sweep/SKILL.md's blind-spots section."
  echo "known baseline: Chorus/Flanger/Phaser/Stutter/Tremolo's sync param used to"
  echo "be baselined alongside rateDiv here too, but is no longer — each kernel's"
  echo "synced-vs-free rateHz/period default now differs enough (e.g. ChorusKernel:"
  echo "0.5Hz free vs 2Hz synced quarter-note) that toggling sync alone is"
  echo "observable within the sweep's window. rateDiv still fails: EffectDefs.cpp"
  echo "gives it no {sync, 1.0f} prerequisite, so for these five (sync defaults"
  echo "false) it's probed with the sync branch dead - the same gated-param blind"
  echo "spot as Audio Filter's gain. Delay itself still fails both sync and"
  echo "rateDiv per the paragraph above (no kernel-timing escape hatch there)."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on all nine Sample"
  echo "Player params (pitch, finetune, speed, start, end, volume, loop, reverse,"
  echo "pingpong - registered type name is still \"Sampler\"). The sweep's generic rig"
  echo "constructs a bare throwaway instance with no file loaded, and an unloaded"
  echo "Sample Player is silent by design (matching Audio File's \"no file loaded\""
  echo "behaviour) - with nothing playing, no param can ever produce a measurable"
  echo "difference in the rendered signature. Confirmed correct by loading a real"
  echo "sample and checking each param (plus reverse/pingpong/end behaviour and the"
  echo "record input) audibly/visibly changes playback; see SAMPLERTEST inside"
  echo "RunSamplerFixture in main.cpp, and SamplerNode.cpp's ProcessBlock."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Note Router's"
  echo "probability (redistributes events across its multiple output ports, but the"
  echo "sweep's generic rig only ever reads port 0 via NoteOutbox()) and on Note"
  echo "Capturer's loop/quantizeDiv (both only take effect via an explicit"
  echo "record/play transport command the generic rig never issues, so the node"
  echo "never leaves its idle state during the probe). Confirmed correct by reading"
  echo "NoteNodes.cpp's AudioNoteRouterNode::NoteOutbox(int) and"
  echo "AudioNoteCapturerNode::ProcessBlock's mCommand switch."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on every Drum"
  echo "Sequencer param (all 8 lanes' laneN_volume/pan/pitch/finetune/decay/"
  echo "transient/start/end/mute/solo/choke and laneN_step0..step7 - the real"
  echo "VisitParams names, not the doc-prose \"steps\" singular this paragraph used"
  echo "to say - plus global rate/steps/swing/volume/run and"
  echo "the four global offset knobs: globalTransient/globalDecay/globalPitch/"
  echo "globalPan)."
  echo "Same root cause as Sampler above: the sweep's generic rig builds a bare"
  echo "throwaway instance with no lane sample loaded, and a lane with nothing"
  echo "loaded is silent by design (docs/plans/audio/drum-sequencer-v2-prompt.md"
  echo "§2) - with nothing playing, no param can produce a measurable difference"
  echo "in the rendered signature. Confirmed correct with real loaded samples in"
  echo "RunDrumSequencerFixture (DRUMSEQTEST) in main.cpp, which exercises every"
  echo "one of these params (timing, swing per-lane and global, velocity, choke,"
  echo "solo/mute, decay, start/end range, global offset composition, voice-"
  echo "steal, transport agreement, run toggle) against real audio and passes."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Plugin's bypass"
  echo "and map_slots. This one is by design, not a rig limitation: a hosted"
  echo "plugin's parameters deliberately do NOT go through ParamMailbox - they are"
  echo "pushed straight to the plugin with Platform::PluginSetParameter, because the"
  echo "plugin owns its own parameter smoothing and one mailbox slot per mapped"
  echo "param would blow past ParamMailbox's 64-slot ceiling (see AudioPluginNode.h)."
  echo "bypass does reach the audio thread, through its own atomic rather than the"
  echo "mailbox the sweep watches, and map_slots is a serialization-only count of"
  echo "how many mapping rows to write - it has no audio meaning at all. The sweep"
  echo "also builds a bare instance with no plugin loaded, which renders pass-through"
  echo "by design, so nothing it can set could change the signature either way."
  echo "Confirmed by INFINITE_PLUGINSCANTEST (real AU: instantiate, render, set and"
  echo "read a parameter, save/restore fullState) and INFINITE_PLUGINDRAGTEST."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Limiter's"
  echo "release. Confirmed by direct simulation of the kernel's own math against"
  echo "the sweep's exact probe/block timing: the probe tone is two channels in"
  echo "phase quadrature (L=sin, R=cos), so the stereo-linked per-sample level"
  echo "ripples with a ~40-sample period, but this kernel's peak detector takes"
  echo "the max over its full ~72-sample lookahead window (wider than that"
  echo "ripple), which lands on a fixed value once warmed up - nothing left for"
  echo "release to chase. threshold has explicit testCandidates and passes; see"
  echo "EffectDefs.cpp's comment on Limiter's release param."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on all eight of Note"
  echo "Stack's semiN params (semi0..semi7 - enabledN itself passes for all eight)."
  echo "Same root cause as Note Router/Sampler above: each semitone only affects the"
  echo "voice it belongs to, and the sweep's generic rig builds a bare throwaway"
  echo "instance with every enabledN false by default, so no voice is sounding for"
  echo "any semiN to be observed on. Confirmed correct by RunNoteStackFixture"
  echo "(DSPTEST notestack ...) in main.cpp, which enables real voices and checks"
  echo "the exact transposed pitches, the dedupe rule, and the out-of-range drop -"
  echo "all against the live semitone values. Note Stack's sweep also now fails a"
  echo "9th param, useGlobalScale - same root cause as semiN: the scale-snap call"
  echo "sits inside 'if (!ens[v]) continue' in NoteNodes.cpp, and every voice is"
  echo "disabled by default, so it's never reached either."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Oscillator,"
  echo "Metallic, Wave Terrain and Wavetable's frequency (all four share one cause:"
  echo "each has a note-input pin, so the sweep's rig always wires a note cable and"
  echo "these nodes only read frequency in their free-running branch, gated on"
  echo "no-note-driven - see OscillatorNode.h's 'ignored when note-driven' comment,"
  echo "WavetableSynthCore.h and MetallicNode.cpp/WaveTerrainNode.cpp's isNoteDriven"
  echo "checks - so free-run mode is structurally untestable by a rig that always"
  echo "holds a note), and on glide for the same three synth-voice types (wired"
  echo "correctly into pitch smoothing, but every new voice snaps its glide state"
  echo "immediately to the trigger pitch - WavetableSynthCore.h's SetImmediate,"
  echo "MetallicResonator.h's pitchSmoother.SetImmediate - and the rig always"
  echo "retriggers the identical note, so there is never a pitch distance to glide"
  echo "across). Also fmMode (gated by fmDepth's 0.0 default), cutoff/resonance"
  echo "(gated by filterType's Off default), and detune/stereoWidth/phaseRandomize"
  echo "on Wavetable's a.* engine (gated by unison's 1 default - phaseRandomize"
  echo "spreads unison voices' start phase same as detune spreads their pitch, so"
  echo "it's a no-op with one voice too - same class WaveTerrain already declares"
  echo "via SweepPrerequisitesFor but Oscillator/Wavetable don't). None of these are"
  echo "dropped mailbox pushes - every one reaches its atomic/ParamMailbox/"
  echo "SmoothedValue read site; confirm by hand: unplug the note pin and sweep"
  echo "frequency, or set unison=2/filterType!=Off/fmDepth>0 before touching the"
  echo "gated param, or trigger two different pitches in sequence for glide."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Wavetable's"
  echo "a.warpMode/a.warpAmount/a.warpRatio (WarpSample in WavetableSynthCore.h"
  echo "returns the input sample unshaped whenever warpAmount is 0, which is"
  echo "a.warpAmount's own default - same gated-by-sibling-default class as"
  echo "fmMode/fmDepth above), a.filterAmount/filterAttack/filterDecay/"
  echo "filterSustain (a.filterAmount also defaults to 0.0 - same exp2f(0)=1"
  echo "reasoning as Wave Terrain's filterAttack/Decay/Sustain/Release below, on"
  echo "top of a.cutoff/a.resonance's own filterType=Off gate - filterRelease is"
  echo "additionally covered by the note-off paragraph below), a.pitchAttack/"
  echo "pitchDecay/pitchSustain/pitchRelease (a.pitchAmount defaults to 0.0"
  echo "semitones, so the pitch envelope's shape multiplies zero regardless of"
  echo "its own ADSR), and a.fine (read live into every voice's pitch every block"
  echo "with no gating - WavetableSynthCore.h line ~446 - but it is a pure"
  echo "frequency shift, and this synth's peak+RMS signature doesn't move enough"
  echo "for one to register within the sweep's epsilon; same class as Spectral"
  echo "Synth's fine below)."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Wave Terrain's"
  echo "ratioA/ratioB (only read by EvaluateOrbit's Lissajous case in"
  echo "WaveTerrainDsp.h - default orbitType is Circle, which ignores both), its"
  echo "filterAttack/Decay/Sustain/Release (WaveTerrainNode.cpp computes"
  echo "modulatedCutoff via exp2f(filterAmount * filtEnv), and filterAmount"
  echo "defaults to 0.0 so exp2f(0)=1 regardless of the envelope), and on"
  echo "ampRelease/filterRelease on Wave Terrain/Oscillator/Wavetable generally"
  echo "(the sweep's PushHeldNoteOn only ever pushes note-ons - there is no"
  echo "note-off anywhere in AudioParamSweep, so release-stage code is"
  echo "unreachable by this rig). Wavetable's a.ampSustain is the same"
  echo "window class from the other end of the envelope: a.ampAttack/ampDecay"
  echo "default to 4ms/300ms, so a held voice doesn't reach the sustain stage"
  echo "until ~304ms in, far longer than Check B's single ~10ms block - matching"
  echo "Reverb's decay/damping/predelay reasoning above (correct, just slower"
  echo "than the sweep's post-alteration window)."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on all ~28 of"
  echo "Wavetable's b.* params (b.on/table/position/volume/pan/unison/detune/"
  echo "stereoWidth/octave/semi/fine/phase/phaseRandomize/warpMode/warpAmount/"
  echo "warpRatio/filterType/cutoff/resonance/ampAttack/Decay/Sustain/Release/"
  echo "pitchAmount/Attack/Decay/Sustain/Release/filterAmount/Attack/Decay/Sustain/"
  echo "Release). One root cause: WavetableNode.cpp defaults engines[1].on to"
  echo "false, and RenderEngine skips a disabled engine entirely (lastOut=0,"
  echo "continue) - same class as Note Stack's enabledN/semiN pattern above. Every"
  echo "b.* param is silent until b.on is flipped true first."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on all 20 Granular"
  echo "params and all 12 PaulStretch params. Same root cause as Sampler/Drum"
  echo "Sequencer above: AudioGranularNode::ProcessBlock computes hasSample from"
  echo "mSampleSlot.Active() and, if false, zeroes the output and returns before"
  echo "any param (including mix/volume/pan) is even read (GranularNode.cpp)."
  echo "AudioPaulStretchNode::ProcessBlock is the same but stricter - it also"
  echo "requires an audition/self-envelope trigger, and returns silent otherwise"
  echo "(PaulStretchNode.cpp) - before any of its 12 params are read. The sweep's"
  echo "bare rig loads no sample and issues no audition, so every param on both"
  echo "nodes is unreachable. Confirm by hand with a real loaded sample."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on 10 Spectral"
  echo "Synth params, for four distinct reasons (not one blanket cause - most of"
  echo "its other params pass, since unlike Granular/PaulStretch this node always"
  echo "has a procedurally-generated default spectrogram to render even with"
  echo "nothing connected). rootFreq is only consumed when freqScale=Harmonic"
  echo "(default is Logarithmic) and position only when scanMode=Manual (default"
  echo "is BpmSync) - both gated-by-sibling-default, same class as Audio Filter's"
  echo "gain, just missing a SweepPrerequisitesFor entry that detune/stereoWidth"
  echo "already have (ImageSpectralSynthNode.h/.cpp). glide is voice-retrigger-only"
  echo "(AllocateVoice seeds currentPitchRatio=targetPitchRatio immediately for any"
  echo "freshly-triggered voice, so there is never a stale pitch to glide from -"
  echo "same class as the Oscillator-family glide entry above). detune, ampDecay,"
  echo "ampSustain, ampRelease, scanMode and rate are wired correctly but only"
  echo "manifest over many milliseconds-to-seconds (unison-detune beating,"
  echo "envelope stages after attack completes, playhead advance per block being a"
  echo "small fraction of image width) - none change the single next block Check B"
  echo "measures, the same structural class as Dynamics' knee/rmsWindow/lookahead."
  echo "fine is the 10th: read live every block into finePitchRatio and applied to"
  echo "both the drone voice and every triggered voice's pitch with no gating"
  echo "(ImageSpectralSynthNode.cpp line ~438) - it is a pure frequency shift on a"
  echo "multi-partial additive tone, which leaves this block's peak+RMS signature"
  echo "essentially unchanged; the same peak/RMS-blind-to-frequency-alone class as"
  echo "the Oscillator/Wave Terrain/Wavetable free-run frequency entries below."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on all 8"
  echo "Arpeggiator params for one shared cause: AudioArpeggiatorNode::ProcessBlock"
  echo "(NoteNodes.cpp) drives its step clock off Transport::Instance().Beats(),"
  echo "but the headless sweep runs before glfwInit()/AudioEngine::Start() and"
  echo "nothing ever calls Transport::Tick(), so Beats() stays frozen at 0 for the"
  echo "whole probe - exactly one step fires (at mLastStep's initial -1 boundary)"
  echo "and never again, so mode/octaves/rateMode/rateBeats/rateSeconds/"
  echo "gatePercent/stepGates' effect on subsequent steps is never observable."
  echo "Arpeggiator's 8th failing param, useGlobalScale, WAS a genuinely dropped"
  echo "feature (stored via mUseGlobalScale.store in PushParams but never"
  echo "loaded/read anywhere in ProcessBlock) - fixed directly in NoteNodes.cpp:"
  echo "the gated step-trigger branch now snaps seqNote[idx] through"
  echo "MusicTime::SnapToScale before pushing the note-on, same call as Note"
  echo "Filter/Note Transpose below. It still reports [FAIL] after the fix, for"
  echo "the same reason those two do (see next paragraph) - the sweep's held"
  echo "chord is already diatonic, so the fix is real but unobservable by this rig."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on useGlobalScale"
  echo "for Note Filter, Note Transpose and Arpeggiator (unlike Note Stack above,"
  echo "this trio IS a rig blind spot): Transport defaults to key=C/scale=major,"
  echo "and the sweep's probe notes (60/64/67 chord or single note 69) already lie"
  echo "on that scale, so snapping is a no-op whether useGlobalScale is on or off"
  echo "(NoteNodes.cpp's AudioNoteFilterNode/AudioSemitoneShiftNode/"
  echo "AudioArpeggiatorNode ProcessBlock)."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Pitch Bend's"
  echo "bendSemitones. Not a dropped mailbox push - AudioPitchBendNode::ProcessBlock"
  echo "writes it into out.bendSemitones correctly (NoteNodes.cpp). The sweep's"
  echo "note-outbox signature (RunNoteWindow/RunOneBlock in main.cpp) only reads"
  echo "note/velocity/isNoteOn/frameOffset off each NoteEvent - bendSemitones isn't"
  echo "part of either signature, so a param whose only effect is that field is"
  echo "structurally invisible here."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Note Echo's"
  echo "repeats/decay/transposePerRepeat (delayMs itself passes). Residual gap left"
  echo "by RunNoteWindow's earlier fix: the window is 12*256=3072 samples (64ms) at"
  echo "the sweep's 48kHz rig, but delayMs's own 150ms default schedules the first"
  echo "repeat at 7200 samples - outside the window regardless of these three"
  echo "params' value, so only the immediate dry passthrough is ever observed."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on Plugin's"
  echo "plugin_accepts_notes (3rd by-design param, alongside bypass/map_slots"
  echo "documented above). mAcceptsNotes is set once from the loaded plugin's own"
  echo "descriptor (AudioPluginNode.cpp) and has no UI control or CookIfNeeded path"
  echo "at all - it's read-only serialized metadata, not a live-settable param."
  echo "known baseline: AUDIOPARAMSWEEPTEST also reports [FAIL] on all seven Audio"
  echo "File params (loop, followTransport, monitor, volume, gain, attack, release"
  echo "- registered type name AudioFileNode, in AnalyzeNodes.h/.cpp). Same root"
  echo "cause as Sampler/Granular/PaulStretch above: AudioFilePlayerAudioNode::"
  echo "ProcessBlock computes hasBuffer from mActiveBuffer and, with the sweep's"
  echo "bare rig loading no file, gates all audible output behind it - matching"
  echo "AudioFileNode's own \"no file loaded\" behaviour comment. followTransport"
  echo "additionally only flips Play()/Pause() when Transport::IsPlaying() changes"
  echo "between cooks, which the sweep's single before/after probe never triggers."
  exit 1
fi
echo "all checks green."
exit 0
