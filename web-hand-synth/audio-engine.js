/**
 * Web Audio Engine for Hand-Tracking Synth
 * Features:
 * - Dual Oscillators (Sawtooth, Square, Triangle, Sine, Pulse, Sub)
 * - 24dB/oct Resonant Filter (Lowpass, Bandpass, Highpass)
 * - Stereo Ping-Pong / Feedback Delay
 * - Algorithmic Lush Reverb
 * - Musical Scale Quantizer (Pentatonic, Dorian, Phrygian, etc.)
 * - Analyser Node for Live Waveform & Spectrum
 */

class SynthAudioEngine {
    constructor() {
        this.ctx = null;
        this.isRunning = false;
        this.isMuted = false;
        
        // Settings & State
        this.baseOctave = 3;
        this.scaleMode = 'minor_pentatonic'; // 'continuous', 'minor_pentatonic', 'dorian', 'cyberpunk', 'major'
        this.waveform = 'sawtooth';
        this.filterType = 'lowpass';
        
        // Active Notes / Params
        this.currentFreq = 220;
        this.currentCutoff = 1200;
        this.currentResonance = 4.0;
        this.volume = 0.6;
        this.droneMode = true; // continuous sound modulated by hands
        
        // Scale definitions (semitones from root C)
        this.scales = {
            continuous: null,
            minor_pentatonic: [0, 3, 5, 7, 10], // C, Eb, F, G, Bb
            dorian: [0, 2, 3, 5, 7, 9, 10],     // C, D, Eb, F, G, A, Bb
            phrygian: [0, 1, 3, 5, 7, 8, 10],   // C, Db, Eb, F, G, Ab, Bb
            cyberpunk: [0, 1, 4, 5, 7, 8, 11],  // Double harmonic minor
            major_pentatonic: [0, 2, 4, 7, 9]   // C, D, E, G, A
        };
        
        this.rootMidi = 36; // C2 base
    }

    async init() {
        if (this.ctx) return;
        
        const AudioContextClass = window.AudioContext || window.webkitAudioContext;
        this.ctx = new AudioContextClass();
        
        // Master Gain & Limiter / Compressor
        this.masterGain = this.ctx.createGain();
        this.masterGain.gain.setValueAtTime(this.volume, this.ctx.currentTime);
        
        this.limiter = this.ctx.createDynamicsCompressor();
        this.limiter.threshold.setValueAtTime(-3, this.ctx.currentTime);
        this.limiter.knee.setValueAtTime(6, this.ctx.currentTime);
        this.limiter.ratio.setValueAtTime(12, this.ctx.currentTime);
        this.limiter.attack.setValueAtTime(0.003, this.ctx.currentTime);
        this.limiter.release.setValueAtTime(0.15, this.ctx.currentTime);
        
        // Analyser for UI Visualizer
        this.analyser = this.ctx.createAnalyser();
        this.analyser.fftSize = 1024;
        this.analyser.smoothingTimeConstant = 0.85;

        // Build Effects Chain (Filter -> Delay & Reverb -> Master)
        this._buildFilter();
        this._buildDelay();
        this._buildReverb();
        
        // Build Oscillators
        this._buildOscillators();

        // Connect master routing:
        // Filter -> Dry Gain -> Limiter
        // Filter -> Delay -> Limiter
        // Filter -> Reverb -> Limiter
        this.filter.connect(this.dryGain);
        this.dryGain.connect(this.limiter);
        
        this.filter.connect(this.delaySend);
        this.delayReturn.connect(this.limiter);
        
        this.filter.connect(this.reverbSend);
        this.reverbReturn.connect(this.limiter);

        this.limiter.connect(this.masterGain);
        this.masterGain.connect(this.analyser);
        this.analyser.connect(this.ctx.destination);

        this.isRunning = true;
    }

    _buildOscillators() {
        // Main Oscillator
        this.osc1 = this.ctx.createOscillator();
        this.osc1.type = this.waveform;
        this.osc1.frequency.setValueAtTime(this.currentFreq, this.ctx.currentTime);
        
        // Sub / Detuned Oscillator 2 for rich analog warmth
        this.osc2 = this.ctx.createOscillator();
        this.osc2.type = 'square';
        this.osc2.frequency.setValueAtTime(this.currentFreq * 0.5, this.ctx.currentTime);
        this.osc2.detune.setValueAtTime(7, this.ctx.currentTime); // +7 cents detune

        this.osc1Gain = this.ctx.createGain();
        this.osc1Gain.gain.setValueAtTime(0.65, this.ctx.currentTime);

        this.osc2Gain = this.ctx.createGain();
        this.osc2Gain.gain.setValueAtTime(0.35, this.ctx.currentTime);

        // Connect oscillators to filter
        this.osc1.connect(this.osc1Gain);
        this.osc1Gain.connect(this.filter);

        this.osc2.connect(this.osc2Gain);
        this.osc2Gain.connect(this.filter);

        this.osc1.start();
        this.osc2.start();
    }

    _buildFilter() {
        this.filter = this.ctx.createBiquadFilter();
        this.filter.type = this.filterType;
        this.filter.frequency.setValueAtTime(this.currentCutoff, this.ctx.currentTime);
        this.filter.Q.setValueAtTime(this.currentResonance, this.ctx.currentTime);

        this.dryGain = this.ctx.createGain();
        this.dryGain.gain.setValueAtTime(0.8, this.ctx.currentTime);
    }

    _buildDelay() {
        this.delaySend = this.ctx.createGain();
        this.delaySend.gain.setValueAtTime(0.4, this.ctx.currentTime);

        this.delayNode = this.ctx.createDelay(2.0);
        this.delayNode.delayTime.setValueAtTime(0.32, this.ctx.currentTime); // ~120 BPM dotted 8th

        this.delayFeedback = this.ctx.createGain();
        this.delayFeedback.gain.setValueAtTime(0.45, this.ctx.currentTime);

        // Feedback loop with damping lowpass filter
        this.delayDamping = this.ctx.createBiquadFilter();
        this.delayDamping.type = 'lowpass';
        this.delayDamping.frequency.setValueAtTime(3500, this.ctx.currentTime);

        this.delayReturn = this.ctx.createGain();
        this.delayReturn.gain.setValueAtTime(0.45, this.ctx.currentTime);

        // Wiring delay loop
        this.delaySend.connect(this.delayNode);
        this.delayNode.connect(this.delayDamping);
        this.delayDamping.connect(this.delayFeedback);
        this.delayFeedback.connect(this.delayNode);
        this.delayDamping.connect(this.delayReturn);
    }

    _buildReverb() {
        this.reverbSend = this.ctx.createGain();
        this.reverbSend.gain.setValueAtTime(0.35, this.ctx.currentTime);

        this.convolver = this.ctx.createConvolver();
        this.convolver.buffer = this._generateImpulseResponse(2.5, 2.0);

        this.reverbReturn = this.ctx.createGain();
        this.reverbReturn.gain.setValueAtTime(0.5, this.ctx.currentTime);

        this.reverbSend.connect(this.convolver);
        this.convolver.connect(this.reverbReturn);
    }

    // Synthesize a smooth stereo algorithmic room/hall impulse response
    _generateImpulseResponse(durationSec, decayFactor) {
        const sampleRate = this.ctx.sampleRate;
        const length = sampleRate * durationSec;
        const impulse = this.ctx.createBuffer(2, length, sampleRate);
        const left = impulse.getChannelData(0);
        const right = impulse.getChannelData(1);

        for (let i = 0; i < length; i++) {
            const t = i / sampleRate;
            const envelope = Math.exp(-t * decayFactor);
            // Diffused noise with stereo decorrelation
            left[i] = (Math.random() * 2 - 1) * envelope;
            right[i] = (Math.random() * 2 - 1) * envelope;
        }

        return impulse;
    }

    async resume() {
        if (this.ctx && this.ctx.state === 'suspended') {
            await this.ctx.resume();
        }
    }

    setMute(mute) {
        this.isMuted = mute;
        if (!this.ctx) return;
        const target = mute ? 0 : this.volume;
        this.masterGain.gain.setTargetAtTime(target, this.ctx.currentTime, 0.03);
    }

    setVolume(val) {
        this.volume = Math.max(0, Math.min(1, val));
        if (this.ctx && !this.isMuted) {
            this.masterGain.gain.setTargetAtTime(this.volume, this.ctx.currentTime, 0.05);
        }
    }

    setWaveform(type) {
        this.waveform = type;
        if (this.osc1) {
            this.osc1.type = type;
        }
    }

    setFilterType(type) {
        this.filterType = type;
        if (this.filter) {
            this.filter.type = type;
        }
    }

    setScaleMode(mode) {
        this.scaleMode = mode;
    }

    // Quantize 0..1 normalized pitch input to musical scale frequencies
    _quantizePitch(normalizedVal) {
        const scale = this.scales[this.scaleMode];
        if (!scale) {
            // Continuous frequency mapping (55Hz C1 to ~880Hz A5)
            const minFreq = 55;
            const maxFreq = 880;
            return minFreq * Math.pow(maxFreq / minFreq, normalizedVal);
        }

        // Map normalized 0..1 across 3 octaves
        const totalNotes = scale.length * 3;
        const index = Math.min(totalNotes - 1, Math.floor(normalizedVal * totalNotes));
        const octaveOffset = Math.floor(index / scale.length);
        const scaleIndex = index % scale.length;
        const midiNote = this.rootMidi + (octaveOffset * 12) + scale[scaleIndex];

        // Convert MIDI note number to Hz: f = 440 * 2^((d - 69) / 12)
        return 440 * Math.pow(2, (midiNote - 69) / 12);
    }

    /**
     * Map Hand Tracking / Polygon Parameters to Synth Controls
     * @param {Object} params
     *   - normalizedX: 0..1 (Polygon center X) -> Frequency / Pitch
     *   - normalizedY: 0..1 (Polygon center Y) -> Filter Cutoff
     *   - areaRatio: 0..1 (Polygon area or width/pinch) -> Filter Resonance (Q)
     *   - pinchDist: 0..1 (Thumb-index distance) -> Delay / Reverb wet depth
     *   - active: boolean (Hands detected or not)
     */
    updateHandModulation(params) {
        if (!this.ctx || !this.isRunning) return;

        const now = this.ctx.currentTime;
        const smoothTime = 0.04; // 40ms audio parameter smoothing

        if (!params.active) {
            // Softly decay volume when hands leave frame
            if (this.droneMode) {
                this.masterGain.gain.setTargetAtTime(0.08, now, 0.2);
            }
            return;
        }

        // 1. Pitch / Frequency (from X position)
        const targetFreq = this._quantizePitch(params.normalizedX);
        this.currentFreq = targetFreq;
        this.osc1.frequency.setTargetAtTime(targetFreq, now, smoothTime);
        this.osc2.frequency.setTargetAtTime(targetFreq * 0.5, now, smoothTime);

        // 2. Filter Cutoff (from Y position, inverted so top = bright, bottom = dark)
        // Logarithmic sweep: 80 Hz -> 12,000 Hz
        const yInverted = 1.0 - Math.max(0, Math.min(1, params.normalizedY));
        const minCutoff = 80;
        const maxCutoff = 12000;
        const targetCutoff = minCutoff * Math.pow(maxCutoff / minCutoff, yInverted);
        this.currentCutoff = targetCutoff;
        this.filter.frequency.setTargetAtTime(targetCutoff, now, smoothTime);

        // 3. Filter Resonance (Q) from Polygon Area / Aspect / Width (0.5 to 22.0)
        const qNorm = Math.max(0, Math.min(1, params.areaRatio));
        const targetQ = 0.5 + Math.pow(qNorm, 1.4) * 20.0;
        this.currentResonance = targetQ;
        this.filter.Q.setTargetAtTime(targetQ, now, smoothTime);

        // 4. Delay / Reverb Wet Depth from Pinch / Distance
        if (params.pinchDist !== undefined) {
            const delayMix = 0.15 + (1.0 - params.pinchDist) * 0.55;
            const reverbMix = 0.15 + params.pinchDist * 0.65;
            this.delaySend.gain.setTargetAtTime(delayMix, now, smoothTime * 2);
            this.reverbSend.gain.setTargetAtTime(reverbMix, now, smoothTime * 2);
        }

        // Restore normal volume when hands are tracking
        if (!this.isMuted) {
            this.masterGain.gain.setTargetAtTime(this.volume, now, 0.05);
        }
    }

    getAnalyserData(timeDomainArray, frequencyArray) {
        if (!this.analyser) return;
        if (timeDomainArray) this.analyser.getByteTimeDomainData(timeDomainArray);
        if (frequencyArray) this.analyser.getByteFrequencyData(frequencyArray);
    }
}

window.SynthAudioEngine = SynthAudioEngine;
