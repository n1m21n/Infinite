/**
 * INFINITE PIANO SYMPHONY & TASTE LEARNING ENGINE (piano-engine.js)
 * -----------------------------------------------------------------------------
 * Infinite Nocturne Symphony: Continuous C++ WASM PianoNode Streamer +
 * Wide-Range (A0–C8 / MIDI 21–108) Grounded Physical Counterpoint Synthesizer +
 * Active Learning Engine (16-Feature UserStyle Bayesian Vector & Beta Bandit).
 *
 * Exposes: window.InfinitePianoEngine
 * Author: Naman Soni (n1m21n) / Infinite Devices
 * -----------------------------------------------------------------------------
 */

(function (global) {
  'use strict';

  // ===========================================================================
  // 1. MUSICAL & PHYSICAL CONSTANTS
  // ===========================================================================
  const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const KEY_NAMES = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B'];
  const KEY_NAMES_FLAT = KEY_NAMES;

  function midiToFreq(m) {
    return 440 * Math.pow(2, (m - 69) / 12);
  }

  function midiToNoteName(m) {
    const pc = ((m % 12) + 12) % 12;
    const oct = Math.floor(m / 12) - 1;
    return NOTE_NAMES[pc] + oct;
  }

  function classifyRegister(midi) {
    if (midi < 48) return 'bass';
    if (midi < 72) return 'mid';
    return 'treble';
  }

  // 16 Taste Features from Infinite's UserStyle.h
  const FEATURE_NAMES = [
    'density',         // 0: event frequency (sparse 0 -> busy 1)
    'syncopation',     // 1: off-grid syncopated rhythm share
    'swing',           // 2: gentle triplet / swing micro-timing
    'bassPresence',    // 3: weight and frequency of bass stratum notes
    'chordRichness',   // 4: chord complexity (triad -> 7th -> 9th -> 11th)
    'melodicLeap',     // 5: melodic interval size (stepwise 0 -> wide leaps 1)
    'registerSpread',  // 6: total pitch span utilization
    'harmonicWarmth',  // 7: preference for warm consonant voicings
    'smoothnessBias',  // 8: soft hammer, legato, long release
    'soothingBias',    // 9: calm tempo, major/dorian/lydian, gentle dynamics
    'spaceBias',       // 10: large reverb tail and tape delay immersion
    'tempoCalmness',   // 11: slow tempo preference (48–60 BPM)
    'motifRepetition', // 12: frequency of recurring melodic themes
    'modalColor',      // 13: preference for modal alterations (Lydian #4, Dorian 6)
    'upperSparkle',    // 14: frequency of crystalline high-octave bell notes
    'groundingWeight'  // 15: heavy root-fifth pedal bass presence
  ];

  // Musical Scales (scale intervals in semitones)
  const SCALES = {
    'major': [0, 2, 4, 5, 7, 9, 11],
    'natural minor': [0, 2, 3, 5, 7, 8, 10],
    'minor': [0, 2, 3, 5, 7, 8, 10],
    'dorian': [0, 2, 3, 5, 7, 9, 10],
    'lydian': [0, 2, 4, 6, 7, 9, 11],
    'major pentatonic': [0, 2, 4, 7, 9],
    'maj_pent': [0, 2, 4, 7, 9],
    'minor pentatonic': [0, 3, 5, 7, 10],
    'min_pent': [0, 3, 5, 7, 10],
    'mixolydian': [0, 2, 4, 5, 7, 9, 10],
    'harmonic minor': [0, 2, 3, 5, 7, 8, 11],
    'phrygian': [0, 1, 3, 5, 7, 8, 10],
    'hirajoshi': [0, 2, 3, 7, 8]
  };

  // Curated Harmonic Progression Corpus from Infinite's Nocturne Library
  // Each progression contains 4 chords with Roman numeral, scale degrees, biome affinity, and inner voicings
  const PROGRESSION_CORPUS = [
    {
      id: 'nocturne_dorian_impression',
      name: 'Dorian Nocturne (Impressionist)',
      biome: 'meadow',
      scale: 'dorian',
      chords: [
        { roman: 'i7', rootDegree: 0, chordName: 'i7', intervals: [0, 3, 7, 10, 14], bassIntervals: [0, 7] },
        { roman: 'IV9', rootDegree: 5, chordName: 'IV9', intervals: [5, 9, 12, 16, 21], bassIntervals: [5, 12] },
        { roman: 'v7', rootDegree: 7, chordName: 'v7', intervals: [7, 10, 14, 17, 21], bassIntervals: [7, 14] },
        { roman: 'i9', rootDegree: 0, chordName: 'i9', intervals: [0, 3, 7, 10, 14], bassIntervals: [0, 12] }
      ]
    },
    {
      id: 'nocturne_felt_neoclassical',
      name: 'Neo-Classical Felt Nocturne',
      biome: 'meadow',
      scale: 'natural minor',
      chords: [
        { roman: 'i9', rootDegree: 0, chordName: 'i9', intervals: [0, 3, 7, 10, 14], bassIntervals: [0, 7] },
        { roman: 'VIadd9', rootDegree: 8, chordName: 'VIadd9', intervals: [8, 12, 15, 17, 22], bassIntervals: [8, 15] },
        { roman: 'III', rootDegree: 3, chordName: 'IIImaj7', intervals: [3, 7, 10, 14, 17], bassIntervals: [3, 10] },
        { roman: 'VII', rootDegree: 10, chordName: 'VII', intervals: [10, 14, 17, 21, 24], bassIntervals: [10, 17] }
      ]
    },
    {
      id: 'nocturne_floating_lydian',
      name: 'Sublime Floating Lydian',
      biome: 'meadow',
      scale: 'lydian',
      chords: [
        { roman: 'Imaj9', rootDegree: 0, chordName: 'Imaj9', intervals: [0, 4, 7, 11, 14], bassIntervals: [0, 7] },
        { roman: 'IVmaj7#11', rootDegree: 5, chordName: 'IVmaj7#11', intervals: [5, 9, 12, 16, 18], bassIntervals: [5, 12] },
        { roman: 'vi9', rootDegree: 9, chordName: 'vi9', intervals: [9, 12, 16, 19, 23], bassIntervals: [9, 16] },
        { roman: 'Vsus4', rootDegree: 7, chordName: 'Vsus4->3', intervals: [7, 12, 14, 17, 19], bassIntervals: [7, 14] }
      ]
    },
    {
      id: 'nocturne_warm_major',
      name: 'Infinite Warm Major',
      biome: 'meadow',
      scale: 'major',
      chords: [
        { roman: 'Imaj9', rootDegree: 0, chordName: 'Imaj9', intervals: [0, 4, 7, 11, 14], bassIntervals: [0, 7] },
        { roman: 'V6', rootDegree: 7, chordName: 'V6/add9', intervals: [7, 11, 14, 16, 21], bassIntervals: [7, 11] },
        { roman: 'vi7', rootDegree: 9, chordName: 'vi7', intervals: [9, 12, 16, 19, 23], bassIntervals: [9, 16] },
        { roman: 'IVmaj7', rootDegree: 5, chordName: 'IVmaj7', intervals: [5, 9, 12, 16, 19], bassIntervals: [5, 12] }
      ]
    },
    {
      id: 'nocturne_pentatonic_zen',
      name: 'Ethereal Pentatonic Horizon',
      biome: 'meadow',
      scale: 'major pentatonic',
      chords: [
        { roman: 'Iadd9', rootDegree: 0, chordName: 'Iadd9', intervals: [0, 4, 7, 14], bassIntervals: [0, 7] },
        { roman: 'iii7', rootDegree: 4, chordName: 'iii7', intervals: [4, 7, 11, 14], bassIntervals: [4, 11] },
        { roman: 'IVadd9', rootDegree: 5, chordName: 'IVadd9', intervals: [5, 9, 12, 16], bassIntervals: [5, 12] },
        { roman: 'vi7', rootDegree: 9, chordName: 'vi7', intervals: [9, 12, 16, 19], bassIntervals: [9, 16] }
      ]
    },
    {
      id: 'nocturne_velvet_minor',
      name: 'Midnight Velvet Minor',
      scale: 'natural minor',
      chords: [
        { roman: 'i9', rootDegree: 0, chordName: 'i9', intervals: [0, 3, 7, 10, 14], bassIntervals: [0, 7] },
        { roman: 'bVImaj7', rootDegree: 8, chordName: 'bVImaj7', intervals: [8, 12, 15, 19], bassIntervals: [8, 15] },
        { roman: 'iv9', rootDegree: 5, chordName: 'iv9', intervals: [5, 8, 12, 15, 19], bassIntervals: [5, 12] },
        { roman: 'bVII9', rootDegree: 10, chordName: 'bVII9', intervals: [10, 14, 17, 21, 24], bassIntervals: [10, 17] }
      ]
    }
  ];

  // 5 Physical Piano Timbre Models (modeled after PianoNode.cpp)
  const PIANO_MODELS = {
    felt: {
      id: 'felt',
      name: 'Intimate Felt Nocturne',
      desc: 'Softest hammer, warm cloth damper, intimate acoustic body',
      hardness: 0.18,
      tone: 0.35,
      stiffness: 0.25,
      attack: 0.024,
      decay: 0.72,
      bodyResonance: 0.85,
      unisonDetune: 1.4,
      hammerCutoff: 1800,
      soundboardMix: 0.85,
      isElectric: false
    },
    grand: {
      id: 'grand',
      name: 'Infinite Concert Grand',
      desc: 'Wide dynamic range, rich spruce soundboard, concert brilliance',
      hardness: 0.62,
      tone: 0.65,
      stiffness: 0.50,
      attack: 0.003,
      decay: 0.88,
      bodyResonance: 0.75,
      unisonDetune: 2.5,
      hammerCutoff: 6500,
      soundboardMix: 0.70,
      isElectric: false
    },
    dark_grand: {
      id: 'dark_grand',
      name: 'Midnight Dark Grand',
      desc: 'Deep sub-bass grounding, velvet upper octaves, rich dark warmth',
      hardness: 0.32,
      tone: 0.42,
      stiffness: 0.42,
      attack: 0.008,
      decay: 0.92,
      bodyResonance: 0.95,
      unisonDetune: 2.0,
      hammerCutoff: 3200,
      soundboardMix: 0.85,
      isElectric: false
    },
    upright: {
      id: 'upright',
      name: 'Cozy Studio Upright',
      desc: 'Woody mid-body resonance, organic mechanical salon warmth',
      hardness: 0.52,
      tone: 0.55,
      stiffness: 0.85,
      attack: 0.005,
      decay: 0.62,
      bodyResonance: 0.65,
      unisonDetune: 3.2,
      hammerCutoff: 4800,
      soundboardMix: 0.65,
      isElectric: false
    },
    rhodes: {
      id: 'rhodes',
      name: 'Warm Tine Electric Piano',
      desc: 'Bell-like tine overtone + warm bark saturation & chorus',
      hardness: 0.40,
      tone: 0.50,
      stiffness: 0.20,
      attack: 0.004,
      decay: 0.80,
      bodyResonance: 0.40,
      unisonDetune: 1.8,
      hammerCutoff: 4000,
      soundboardMix: 0.35,
      isElectric: true,
      tineBell: 0.65,
      bark: 0.45
    }
  };

  // ===========================================================================
  // 2. ACTIVE TASTE LEARNING MODEL (UserStyle.cpp Bayesian Bandit)
  // ===========================================================================
  class TasteLearningModel {
    constructor() {
      this.dim = 16;
      this.featW = new Float32Array(this.dim);
      this.featPrec = new Float32Array(this.dim);
      for (let i = 0; i < this.dim; i++) this.featPrec[i] = 2.0;
      this.featN = 0.0;
      this.phrasesLearned = 0;
      this.totalListeningSec = 0;
      this.recentDecisions = [];

      // Beta posteriors for Keys (12), Scales, Progressions, Models
      this.keyAlpha = new Float32Array(12).fill(1.0);
      this.keyBeta = new Float32Array(12).fill(1.0);
      this.scalePosterior = {
        'dorian': { a: 2.5, b: 1.0 },
        'natural minor': { a: 2.0, b: 1.0 },
        'lydian': { a: 1.8, b: 1.0 },
        'major': { a: 1.8, b: 1.0 },
        'major pentatonic': { a: 1.5, b: 1.0 },
        'minor pentatonic': { a: 1.5, b: 1.0 }
      };
      this.progPosterior = {};
      PROGRESSION_CORPUS.forEach((p) => {
        this.progPosterior[p.id] = { a: 1.5, b: 1.0 };
      });
      this.modelPosterior = {
        felt: { a: 2.5, b: 1.0 },
        dark_grand: { a: 2.2, b: 1.0 },
        grand: { a: 2.0, b: 1.0 },
        upright: { a: 1.8, b: 1.0 },
        rhodes: { a: 1.5, b: 1.0 }
      };

      this.loadStorage();
    }

    loadStorage() {
      if (typeof localStorage === 'undefined') return;
      try {
        const raw = localStorage.getItem('infinite_piano_taste_v1') || localStorage.getItem('infinite_taste_v1');
        if (!raw) return;
        const data = JSON.parse(raw);
        if (data.featW && data.featW.length === this.dim) {
          for (let i = 0; i < this.dim; i++) {
            this.featW[i] = data.featW[i] || 0;
            this.featPrec[i] = data.featPrec ? data.featPrec[i] || 2.0 : 2.0;
          }
        }
        if (typeof data.featN === 'number') this.featN = data.featN;
        if (typeof data.phrasesLearned === 'number') this.phrasesLearned = data.phrasesLearned;
        if (typeof data.totalListeningSec === 'number') this.totalListeningSec = data.totalListeningSec;
        if (Array.isArray(data.keyAlpha)) {
          for (let i = 0; i < 12; i++) {
            this.keyAlpha[i] = data.keyAlpha[i] || 1.0;
            this.keyBeta[i] = data.keyBeta[i] || 1.0;
          }
        }
        if (data.modelPosterior) Object.assign(this.modelPosterior, data.modelPosterior);
        if (data.scalePosterior) Object.assign(this.scalePosterior, data.scalePosterior);
        if (Array.isArray(data.recentDecisions)) this.recentDecisions = data.recentDecisions.slice(-15);
      } catch (err) {
        // Storage corrupt or private browsing blocked; fallback to defaults
      }
    }

    saveStorage() {
      if (typeof localStorage === 'undefined') return;
      try {
        const payload = {
          version: 1,
          featW: Array.from(this.featW),
          featPrec: Array.from(this.featPrec),
          featN: this.featN,
          phrasesLearned: this.phrasesLearned,
          totalListeningSec: Math.floor(this.totalListeningSec),
          keyAlpha: Array.from(this.keyAlpha),
          keyBeta: Array.from(this.keyBeta),
          scalePosterior: this.scalePosterior,
          modelPosterior: this.modelPosterior,
          recentDecisions: this.recentDecisions.slice(-15),
          updatedAt: Date.now()
        };
        const str = JSON.stringify(payload);
        localStorage.setItem('infinite_piano_taste_v1', str);
        localStorage.setItem('infinite_taste_v1', str);
      } catch (err) {
        // Ignore quota exceptions
      }
    }

    // Bayesian precision and weight update from UserStyle.cpp (lines 564–585)
    updateFeatureWeights(phi, reward, reason = 'feedback') {
      const y = reward >= 0 ? 1.0 : -1.0;
      const amount = Math.min(2.0, Math.abs(reward));

      // dot = sum(w_i * x_i)
      let dot = 0.0;
      for (let i = 0; i < this.dim; i++) {
        const x = (i === this.dim - 1) ? 0.1 : (Math.max(-1.0, Math.min(1.0, phi[i])) - 0.35) * 2.0;
        dot += this.featW[i] * x;
      }
      dot = Math.max(-8.0, Math.min(8.0, dot));
      const pred = 1.0 / (1.0 + Math.exp(-y * dot));
      const err = (1.0 - pred) * y * amount;

      for (let i = 0; i < this.dim; i++) {
        const x = (i === this.dim - 1) ? 0.1 : (Math.max(-1.0, Math.min(1.0, phi[i])) - 0.35) * 2.0;
        this.featPrec[i] += 0.15 * x * x;
        this.featW[i] += (0.65 / Math.sqrt(Math.max(0.5, this.featPrec[i]))) * err * x;
      }
      this.featN += amount;
      this.phrasesLearned++;

      this.recentDecisions.push({
        timestamp: Date.now(),
        reward,
        reason,
        adaptationScore: this.getAdaptationScore()
      });
      if (this.recentDecisions.length > 20) this.recentDecisions.shift();

      this.saveStorage();
    }

    recordBetaFeedback(key, scale, progId, modelId, reward) {
      const pos = reward > 0;
      const amt = Math.abs(reward);

      if (key >= 0 && key < 12) {
        if (pos) this.keyAlpha[key] += amt;
        else this.keyBeta[key] += amt;
      }
      if (scale && this.scalePosterior[scale]) {
        if (pos) this.scalePosterior[scale].a += amt;
        else this.scalePosterior[scale].b += amt;
      }
      if (progId && this.progPosterior[progId]) {
        if (pos) this.progPosterior[progId].a += amt;
        else this.progPosterior[progId].b += amt;
      }
      if (modelId && this.modelPosterior[modelId]) {
        if (pos) this.modelPosterior[modelId].a += amt;
        else this.modelPosterior[modelId].b += amt;
      }

      this.saveStorage();
    }

    getAdaptationScore() {
      return Math.min(1.0, this.featN / (this.featN + 6.0));
    }

    // Sample from Beta distribution using approximation
    sampleBeta(a, b) {
      // Normal approximation / pseudo-random Gamma draw
      const u1 = Math.random() || 0.0001;
      const u2 = Math.random() || 0.0001;
      const mean = a / (a + b);
      const variance = (a * b) / ((a + b) * (a + b) * (a + b + 1));
      const std = Math.sqrt(variance);
      const z = Math.sqrt(-2.0 * Math.log(u1)) * Math.cos(2.0 * Math.PI * u2);
      return Math.max(0.01, Math.min(0.99, mean + z * std));
    }

    // Thompson Sampling for Next Key
    samplePreferredKey() {
      let bestIdx = 2; // D default
      let maxDraw = -1;
      for (let k = 0; k < 12; k++) {
        const draw = this.sampleBeta(this.keyAlpha[k], this.keyBeta[k]);
        if (draw > maxDraw) {
          maxDraw = draw;
          bestIdx = k;
        }
      }
      return bestIdx;
    }

    // Thompson Sampling for Next Scale
    samplePreferredScale() {
      let best = 'dorian';
      let maxDraw = -1;
      for (const [sc, beta] of Object.entries(this.scalePosterior)) {
        const draw = this.sampleBeta(beta.a, beta.b);
        if (draw > maxDraw) {
          maxDraw = draw;
          best = sc;
        }
      }
      return best;
    }

    // Thompson Sampling for Next Model
    samplePreferredModel() {
      let best = 'felt';
      let maxDraw = -1;
      for (const [m, beta] of Object.entries(this.modelPosterior)) {
        const draw = this.sampleBeta(beta.a, beta.b);
        if (draw > maxDraw) {
          maxDraw = draw;
          best = m;
        }
      }
      return best;
    }

    getSnapshot(currentSettings = {}) {
      const normPhi = [];
      for (let i = 0; i < this.dim; i++) {
        const w = this.featW[i];
        normPhi.push(Math.max(0.05, Math.min(0.95, 0.5 + w * 0.28)));
      }

      // Find top key
      let maxKeyScore = -1, prefKey = 2;
      for (let i = 0; i < 12; i++) {
        const sc = this.keyAlpha[i] / (this.keyAlpha[i] + this.keyBeta[i]);
        if (sc > maxKeyScore) {
          maxKeyScore = sc;
          prefKey = i;
        }
      }

      // Metrics mapped cleanly for UI progress bars (0..1)
      const metrics = {
        grounding: currentSettings.grounding !== undefined ? currentSettings.grounding : normPhi[15],
        smoothness: currentSettings.smoothness !== undefined ? currentSettings.smoothness : normPhi[8],
        soothing: currentSettings.soothingness !== undefined ? currentSettings.soothingness : normPhi[9],
        space: currentSettings.space !== undefined ? currentSettings.space : normPhi[10],
        warmth: normPhi[7],
        canopy: currentSettings.noteRangeWidth !== undefined ? currentSettings.noteRangeWidth : normPhi[6]
      };

      return {
        phi: normPhi,
        featW: Array.from(this.featW),
        featureNames: FEATURE_NAMES,
        metrics,
        preferredKey: KEY_NAMES[prefKey],
        preferredKeyIndex: prefKey,
        preferredScale: this.samplePreferredScale(),
        preferredModel: this.samplePreferredModel(),
        totalListeningSec: Math.floor(this.totalListeningSec),
        phrasesLearned: this.phrasesLearned,
        adaptationScore: this.getAdaptationScore(),
        recentDecisions: this.recentDecisions.slice(-10)
      };
    }

    reset() {
      this.featW.fill(0);
      this.featPrec.fill(2.0);
      this.featN = 0;
      this.phrasesLearned = 0;
      this.keyAlpha.fill(1.0);
      this.keyBeta.fill(1.0);
      if (typeof localStorage !== 'undefined') {
        try {
          localStorage.removeItem('infinite_piano_taste_v1');
        } catch (e) {}
      }
    }
  }

  // ===========================================================================
  // 3. INFINITE PIANO ENGINE MAIN CLASS
  // ===========================================================================
  class InfinitePianoEngine {
    constructor(options = {}) {
      this.options = options;

      // Audio Context & Graph
      this.audioCtx = null;
      this.isStarted = false;
      this.isPlayingState = false;

      // User Knobs & Defaults (strictly aligned with requirements)
      this.smoothness = 0.78;     // 0..1 (default 0.78)
      this.soothingness = 0.82;   // 0..1 (default 0.82)
      this.space = 0.72;          // 0..1 (default 0.72)
      this.noteRangeWidth = 0.88; // 0..1 (default 0.88, A0..C8 full range)
      this.pianoModel = 'felt';   // 'felt' | 'grand' | 'dark_grand' | 'upright' | 'rhodes'
      this.worldBiome = 'meadow'; // 'meadow' | 'glacial' | 'arid'

      // Harmonic State
      this.key = 2; // D (Dorian / D Minor is standard Nocturne)
      this.scale = 'dorian';
      this.currentProgression = PROGRESSION_CORPUS[0];
      this.currentBarIndex = 0;
      this.phraseIndex = 1;
      this.currentBpm = 54; // Dynamically mapped from soothingness (74 -> 48)

      // Space Bus Fine Controls
      this.spaceParams = {
        reverbDecay: 5.5,
        reverbMix: 0.48,
        delayMix: 0.38,
        delayTime: 0.65,
        shimmer: 0.42
      };

      // Volumes
      this.masterVolume = 0.88;
      this.physicalVolume = 0.85;
      this.wasmVolume = 0.75;

      // Active Learning Engine
      this.taste = new TasteLearningModel();

      // WASM Worker & Streamer State
      this.worker = null;
      this.wasmStatus = 'uninitialized';
      this.wasmStyles = [];
      this.wasmScales = [];
      this.lastPatch = '';
      this.lastLabel = 'PianoNode';
      this.lastWhy = 'Continuous C++ WASM Piano Streamer';
      this.lastAttempt = 1;
      this.wasmJobId = 0;
      this.wasmPending = false;
      this.wasmNextStartTime = 0;
      this.wasmPhraseQueue = [];
      this.wasmAheadBuffer = null;

      // Generative Scheduler State
      this.schedulerTimer = null;
      this.dwellTimer = null;
      this.sliderReinforceTimer = null;
      this.listeningSeconds = 0;
      this.nextBarTime = 0;
      this.lookaheadIntervalMs = 40;
      this.scheduleLookaheadSec = 0.16;
      this.voicePool = [];
      this.maxVoices = 28;

      // Melodic Motif Memory
      this.activeMotif = [0, 2, 4, 7];
      this.motifBarCount = 0;

      // Callbacks
      this.callbacks = {
        note: [],
        bar: [],
        wasm: [],
        taste: []
      };

      // Auto-load taste preferences
      this.applyTastePreferences();
    }

    // -------------------------------------------------------------------------
    // Audio Context & Graph Setup
    // -------------------------------------------------------------------------
    initAudioGraph() {
      if (this.audioCtx) return;

      const AudioContextClass = global.AudioContext || global.webkitAudioContext;
      if (!AudioContextClass) {
        console.warn('InfinitePianoEngine: Web Audio API not supported.');
        return;
      }

      this.audioCtx = new AudioContextClass();

      // Master output node
      this.masterGain = this.audioCtx.createGain();
      this.masterGain.gain.setValueAtTime(this.masterVolume, this.audioCtx.currentTime);

      // Submix Gains
      this.physicalGain = this.audioCtx.createGain();
      this.physicalGain.gain.setValueAtTime(this.physicalVolume, this.audioCtx.currentTime);

      this.wasmGain = this.audioCtx.createGain();
      this.wasmGain.gain.setValueAtTime(this.wasmVolume, this.audioCtx.currentTime);

      // Velvet Soothing Lowpass & Ear-Fatigue Dip (2.8 kHz notch filter)
      this.soothingFilter = this.audioCtx.createBiquadFilter();
      this.soothingFilter.type = 'lowpass';
      this.soothingFilter.Q.setValueAtTime(0.707, this.audioCtx.currentTime);

      this.fatigueNotch = this.audioCtx.createBiquadFilter();
      this.fatigueNotch.type = 'peaking';
      this.fatigueNotch.frequency.setValueAtTime(2800, this.audioCtx.currentTime);
      this.fatigueNotch.Q.setValueAtTime(1.4, this.audioCtx.currentTime);
      this.fatigueNotch.gain.setValueAtTime(-3.2, this.audioCtx.currentTime); // -3.2 dB dip at 2.8 kHz

      // Spruce Soundboard Body Formant Bank (PianoNode.cpp Suzuki / Ege 1986 modes)
      this.initSoundboardFormants();

      // Studio-Grade Infinite Space Bus (Cathedral Convolution + Ping-Pong Delay)
      this.initSpaceBus();

      // Analyser Node for Spectrum & Waveform visualizers
      this.analyser = this.audioCtx.createAnalyser();
      this.analyser.fftSize = 1024;
      this.analyser.smoothingTimeConstant = 0.82;

      // Routing:
      // Physical voices -> Soundboard -> physicalGain -> masterSubmix
      // WASM stems -> wasmGain -> masterSubmix
      // masterSubmix -> soothingFilter -> fatigueNotch -> masterGain -> analyser -> destination
      // masterSubmix -> spaceSend -> Space Bus -> masterGain
      // spaceInputNode (external ambient engine) -> Space Bus -> masterGain

      this.masterSubmix = this.audioCtx.createGain();
      this.soundboardOutput.connect(this.physicalGain);
      this.physicalGain.connect(this.masterSubmix);
      this.wasmGain.connect(this.masterSubmix);

      this.masterSubmix.connect(this.soothingFilter);
      this.soothingFilter.connect(this.fatigueNotch);
      this.fatigueNotch.connect(this.masterGain);
      this.masterGain.connect(this.analyser);

      // Output bus shared with the ambient engine: trim -> glue compressor ->
      // brick-wall limiter -> destination, so the sum can never clip.
      const ac = this.audioCtx;
      this.outBus = ac.createGain();
      this.outBus.gain.setValueAtTime(0.7, ac.currentTime);
      this.glueComp = ac.createDynamicsCompressor();
      this.glueComp.threshold.setValueAtTime(-20, ac.currentTime);
      this.glueComp.knee.setValueAtTime(18, ac.currentTime);
      this.glueComp.ratio.setValueAtTime(3, ac.currentTime);
      this.glueComp.attack.setValueAtTime(0.01, ac.currentTime);
      this.glueComp.release.setValueAtTime(0.25, ac.currentTime);
      this.limiter = ac.createDynamicsCompressor();
      this.limiter.threshold.setValueAtTime(-4, ac.currentTime);
      this.limiter.knee.setValueAtTime(0, ac.currentTime);
      this.limiter.ratio.setValueAtTime(20, ac.currentTime);
      this.limiter.attack.setValueAtTime(0.002, ac.currentTime);
      this.limiter.release.setValueAtTime(0.1, ac.currentTime);
      this.analyser.connect(this.outBus);
      this.outBus.connect(this.glueComp);
      this.glueComp.connect(this.limiter);
      this.limiter.connect(ac.destination);

      // Wet Reverb/Delay Send
      this.spaceSend = this.audioCtx.createGain();
      this.masterSubmix.connect(this.spaceSend);
      this.spaceSend.connect(this.spaceInputGain);

      // Apply initial filters & knobs
      this.updateSoothingFilter();
      this.updateSpaceKnob();
    }

    // -------------------------------------------------------------------------
    // Spruce Soundboard Formants (110Hz, 245Hz, 520Hz, 1150Hz from PianoNode.cpp)
    // -------------------------------------------------------------------------
    initSoundboardFormants() {
      const ctx = this.audioCtx;

      this.soundboardInput = ctx.createGain();
      this.soundboardOutput = ctx.createGain();

      // Direct acoustic path (kBoardDirect = 0.65 in PianoNode.cpp)
      const directGain = ctx.createGain();
      directGain.gain.setValueAtTime(0.65, ctx.currentTime);
      this.soundboardInput.connect(directGain);
      directGain.connect(this.soundboardOutput);

      // Low-cut highpass to remove sub-rumble thumps
      const lowCut = ctx.createBiquadFilter();
      lowCut.type = 'highpass';
      lowCut.frequency.setValueAtTime(45, ctx.currentTime);
      lowCut.Q.setValueAtTime(0.707, ctx.currentTime);
      this.soundboardInput.connect(lowCut);

      // Mode 1: 110 Hz Soundboard fundamental
      const f110 = ctx.createBiquadFilter();
      f110.type = 'peaking';
      f110.frequency.setValueAtTime(110, ctx.currentTime);
      f110.Q.setValueAtTime(2.2, ctx.currentTime);
      f110.gain.setValueAtTime(3.5, ctx.currentTime);

      // Mode 2: 245 Hz Main bridge plate resonance
      const f245 = ctx.createBiquadFilter();
      f245.type = 'peaking';
      f245.frequency.setValueAtTime(245, ctx.currentTime);
      f245.Q.setValueAtTime(3.0, ctx.currentTime);
      f245.gain.setValueAtTime(4.0, ctx.currentTime);

      // Mode 3: 520 Hz Mid rib resonance
      const f520 = ctx.createBiquadFilter();
      f520.type = 'peaking';
      f520.frequency.setValueAtTime(520, ctx.currentTime);
      f520.Q.setValueAtTime(2.5, ctx.currentTime);
      f520.gain.setValueAtTime(3.0, ctx.currentTime);

      // Mode 4: 1150 Hz Upper spruce rib overtone
      const f1150 = ctx.createBiquadFilter();
      f1150.type = 'peaking';
      f1150.frequency.setValueAtTime(1150, ctx.currentTime);
      f1150.Q.setValueAtTime(2.0, ctx.currentTime);
      f1150.gain.setValueAtTime(2.5, ctx.currentTime);

      // Modal resonance branch gain (kBoardGain = 0.45 in PianoNode.cpp)
      const modalGain = ctx.createGain();
      modalGain.gain.setValueAtTime(0.45, ctx.currentTime);

      lowCut.connect(f110);
      f110.connect(f245);
      f245.connect(f520);
      f520.connect(f1150);
      f1150.connect(modalGain);
      modalGain.connect(this.soundboardOutput);
    }

    // -------------------------------------------------------------------------
    // Studio-Grade Infinite Space Bus (Cathedral Convolution + Ping-Pong Delay)
    // -------------------------------------------------------------------------
    initSpaceBus() {
      const ctx = this.audioCtx;

      // Public input node accessible via getSpaceInputNode()
      this.spaceInputGain = ctx.createGain();
      this.spaceOutputGain = ctx.createGain();
      this.spaceOutputGain.gain.setValueAtTime(1.0, ctx.currentTime);
      this.spaceOutputGain.connect(this.masterGain);

      // --- A. Procedural Cathedral / Night-Sky Convolution Reverb ---
      this.convolver = ctx.createConvolver();
      this.generateCathedralImpulseResponse(this.spaceParams.reverbDecay || 5.5);

      this.reverbGain = ctx.createGain();
      this.reverbGain.gain.setValueAtTime(this.spaceParams.reverbMix, ctx.currentTime);

      this.spaceInputGain.connect(this.convolver);
      this.convolver.connect(this.reverbGain);
      this.reverbGain.connect(this.spaceOutputGain);

      // --- B. Tempo-Synced Stereo Ping-Pong Tape Delay ---
      const beatSec = 60 / Math.max(40, this.currentBpm);
      const dotted8th = beatSec * 0.75;
      const quarter = beatSec;

      this.delayLeft = ctx.createDelay(4.0);
      this.delayLeft.delayTime.setValueAtTime(dotted8th, ctx.currentTime);

      this.delayRight = ctx.createDelay(4.0);
      this.delayRight.delayTime.setValueAtTime(quarter, ctx.currentTime);

      // Cross-feedback gains
      this.feedbackLR = ctx.createGain();
      this.feedbackLR.gain.setValueAtTime(0.36, ctx.currentTime);

      this.feedbackRL = ctx.createGain();
      this.feedbackRL.gain.setValueAtTime(0.36, ctx.currentTime);

      // Warm tape lowpass & highpass filters in delay loop
      this.delayHp = ctx.createBiquadFilter();
      this.delayHp.type = 'highpass';
      this.delayHp.frequency.setValueAtTime(180, ctx.currentTime);

      this.delayLp = ctx.createBiquadFilter();
      this.delayLp.type = 'lowpass';
      this.delayLp.frequency.setValueAtTime(2800, ctx.currentTime);

      // Subtle Tape Wow & Flutter LFO (0.32 Hz sine modulating delay by ~1.6ms)
      this.wowLfo = ctx.createOscillator();
      this.wowLfo.frequency.setValueAtTime(0.32, ctx.currentTime);
      this.wowDepth = ctx.createGain();
      this.wowDepth.gain.setValueAtTime(0.0016, ctx.currentTime);
      this.wowLfo.connect(this.wowDepth);
      this.wowDepth.connect(this.delayLeft.delayTime);
      this.wowDepth.connect(this.delayRight.delayTime);
      this.wowLfo.start();

      // Tape Saturation Waveshaper (gentle tanh curve)
      this.delaySat = ctx.createWaveShaper();
      this.delaySat.curve = this.makeSaturationCurve(256, 1.4);

      // Stereo Delay Panning
      const splitter = ctx.createChannelSplitter(2);
      const merger = ctx.createChannelMerger(2);

      this.spaceInputGain.connect(this.delayLeft);
      this.spaceInputGain.connect(this.delayRight);

      // Ping-pong cross loops
      this.delayLeft.connect(this.delayHp);
      this.delayHp.connect(this.delayLp);
      this.delayLp.connect(this.delaySat);
      this.delaySat.connect(this.feedbackLR);
      this.feedbackLR.connect(this.delayRight);

      this.delayRight.connect(this.feedbackRL);
      this.feedbackRL.connect(this.delayLeft);

      // Delay outputs into merger
      this.delayLeft.connect(merger, 0, 0);
      this.delayRight.connect(merger, 0, 1);

      this.delayGain = ctx.createGain();
      this.delayGain.gain.setValueAtTime(this.spaceParams.delayMix, ctx.currentTime);
      merger.connect(this.delayGain);
      this.delayGain.connect(this.spaceOutputGain);
    }

    // Generates a 5.5-second modulated stereo velvet impulse response with high-octave shimmer
    generateCathedralImpulseResponse(decaySeconds) {
      if (!this.audioCtx) return;
      const sr = this.audioCtx.sampleRate;
      const length = Math.floor(Math.max(1.0, decaySeconds) * sr);
      const ir = this.audioCtx.createBuffer(2, length, sr);
      const chL = ir.getChannelData(0);
      const chR = ir.getChannelData(1);

      // Modulated velvet noise with exponential decay & air absorption
      const decayTime = Math.max(1.5, decaySeconds);
      const earlyTaps = [
        { delay: Math.floor(0.012 * sr), gain: 0.65, sign: 1 },
        { delay: Math.floor(0.024 * sr), gain: 0.52, sign: -1 },
        { delay: Math.floor(0.038 * sr), gain: 0.44, sign: 1 },
        { delay: Math.floor(0.055 * sr), gain: 0.38, sign: -1 },
        { delay: Math.floor(0.078 * sr), gain: 0.32, sign: 1 },
        { delay: Math.floor(0.110 * sr), gain: 0.28, sign: -1 }
      ];

      for (let t = 0; t < earlyTaps.length; t++) {
        const tap = earlyTaps[t];
        if (tap.delay < length) {
          chL[tap.delay] += tap.gain * tap.sign * 0.8;
          const rDelay = Math.min(length - 1, tap.delay + Math.floor(0.003 * sr));
          chR[rDelay] += tap.gain * (-tap.sign) * 0.8;
        }
      }

      // Diffuse velvet decay tail
      let prevL = 0, prevR = 0;
      const shimmer = this.spaceParams.shimmer || 0.4;

      for (let i = 0; i < length; i++) {
        const t = i / sr;
        const env = Math.exp(-t / (decayTime * 0.42));

        // Velvet pulse density increases with time
        if (Math.random() < 0.28) {
          const signL = Math.random() > 0.5 ? 1 : -1;
          const signR = Math.random() > 0.5 ? 1 : -1;

          // Shimmer high-octave component
          const shimmerMod = Math.sin(2.0 * Math.PI * 880 * t) * shimmer * 0.25;

          const rawL = (signL * (0.4 + Math.random() * 0.6) + shimmerMod) * env;
          const rawR = (signR * (0.4 + Math.random() * 0.6) - shimmerMod) * env;

          // One-pole smoothing to simulate air absorption of high frequencies
          prevL = prevL * 0.35 + rawL * 0.65;
          prevR = prevR * 0.35 + rawR * 0.65;

          chL[i] += prevL;
          chR[i] += prevR;
        } else {
          prevL *= 0.82;
          prevR *= 0.82;
          chL[i] += prevL;
          chR[i] += prevR;
        }
      }

      this.convolver.buffer = ir;
    }

    makeSaturationCurve(samples = 256, k = 1.4) {
      const curve = new Float32Array(samples);
      for (let i = 0; i < samples; ++i) {
        const x = (i * 2) / samples - 1;
        curve[i] = Math.tanh(k * x) / Math.tanh(k);
      }
      return curve;
    }

    // -------------------------------------------------------------------------
    // Real-Time Control Setters (Instant <10ms response)
    // -------------------------------------------------------------------------
    setSmoothness(val) {
      this.smoothness = Math.max(0.0, Math.min(1.0, parseFloat(val) || 0.78));
      this.updateSoothingFilter();
      this.taste.updateFeatureWeights(this.getCurrentTasteVector(), 0.15, 'smoothness_adjust');
      this.dispatchTasteUpdate();
    }

    getSmoothness() {
      return this.smoothness;
    }

    setSoothingness(val) {
      this.soothingness = Math.max(0.0, Math.min(1.0, parseFloat(val) || 0.82));
      // Tempo scales from 74 BPM down to 48 BPM
      this.currentBpm = Math.round(74 - this.soothingness * 26 + (this.taste.featW[11] || 0) * (-4));
      this.updateSoothingFilter();
      this.updateDelayTempo();
      this.taste.updateFeatureWeights(this.getCurrentTasteVector(), 0.15, 'soothingness_adjust');
      this.dispatchTasteUpdate();
    }

    getSoothingness() {
      return this.soothingness;
    }

    setSpace(val) {
      this.space = Math.max(0.0, Math.min(1.0, parseFloat(val) || 0.72));
      this.updateSpaceKnob();
      this.taste.updateFeatureWeights(this.getCurrentTasteVector(), 0.15, 'space_adjust');
      this.dispatchTasteUpdate();
    }

    getSpace() {
      return this.space;
    }

    setSpaceParams(params = {}) {
      if (params.reverbDecay !== undefined) {
        this.spaceParams.reverbDecay = Math.max(1.0, Math.min(12.0, parseFloat(params.reverbDecay)));
        this.generateCathedralImpulseResponse(this.spaceParams.reverbDecay);
      }
      if (params.reverbMix !== undefined) {
        this.spaceParams.reverbMix = Math.max(0.0, Math.min(1.0, parseFloat(params.reverbMix)));
        if (this.reverbGain && this.audioCtx) {
          this.reverbGain.gain.setTargetAtTime(this.spaceParams.reverbMix, this.audioCtx.currentTime, 0.05);
        }
      }
      if (params.delayMix !== undefined) {
        this.spaceParams.delayMix = Math.max(0.0, Math.min(1.0, parseFloat(params.delayMix)));
        if (this.delayGain && this.audioCtx) {
          this.delayGain.gain.setTargetAtTime(this.spaceParams.delayMix, this.audioCtx.currentTime, 0.05);
        }
      }
      if (params.delayTime !== undefined) {
        this.spaceParams.delayTime = Math.max(0.05, Math.min(3.0, parseFloat(params.delayTime)));
        if (this.delayLeft && this.audioCtx) {
          this.delayLeft.delayTime.setTargetAtTime(this.spaceParams.delayTime * 0.75, this.audioCtx.currentTime, 0.05);
          this.delayRight.delayTime.setTargetAtTime(this.spaceParams.delayTime, this.audioCtx.currentTime, 0.05);
        }
      }
      if (params.shimmer !== undefined) {
        this.spaceParams.shimmer = Math.max(0.0, Math.min(1.0, parseFloat(params.shimmer)));
      }
    }

    getSpaceParams() {
      return Object.assign({}, this.spaceParams);
    }

    setNoteRangeWidth(val) {
      this.noteRangeWidth = Math.max(0.1, Math.min(1.0, parseFloat(val) || 0.88));
      this.taste.updateFeatureWeights(this.getCurrentTasteVector(), 0.15, 'range_adjust');
      this.dispatchTasteUpdate();
    }

    getNoteRangeWidth() {
      return this.noteRangeWidth;
    }

    setPianoModel(modelId) {
      if (PIANO_MODELS[modelId]) {
        this.pianoModel = modelId;
        this.taste.recordBetaFeedback(this.key, this.scale, this.currentProgression.id, modelId, 0.35);
        this.dispatchTasteUpdate();
      }
    }

    getPianoModel() {
      return this.pianoModel;
    }

    getPianoModels() {
      return Object.values(PIANO_MODELS);
    }

    setKey(key) {
      if (typeof key === 'string') {
        const idx = NOTE_NAMES.indexOf(key.toUpperCase());
        if (idx !== -1) this.key = idx;
      } else if (typeof key === 'number') {
        this.key = ((key % 12) + 12) % 12;
      }
    }

    getKey() {
      return NOTE_NAMES[this.key];
    }

    setScale(scaleName) {
      if (SCALES[scaleName]) {
        this.scale = scaleName;
        // Find matching progression in corpus
        const match = PROGRESSION_CORPUS.find((p) => p.scale === scaleName);
        if (match) this.currentProgression = match;
      }
    }

    getScale() {
      return this.scale;
    }

    setHarmonyPreference(key, scale) {
      if (key !== undefined && key !== null) {
        if (key === -1 || key === 'auto') {
          this.key = this.taste.samplePreferredKey();
        } else {
          this.setKey(key);
        }
      }
      if (scale) {
        if (scale === 'auto') {
          this.scale = this.taste.samplePreferredScale();
        } else {
          this.setScale(scale);
        }
      }
    }

    setMasterVolume(val) {
      this.masterVolume = Math.max(0.0, Math.min(1.0, parseFloat(val) || 0.8));
      if (this.masterGain && this.audioCtx) {
        this.masterGain.gain.setTargetAtTime(this.masterVolume, this.audioCtx.currentTime, 0.05);
      }
    }

    getMasterVolume() {
      return this.masterVolume;
    }

    setWasmVolume(val) {
      this.wasmVolume = Math.max(0.0, Math.min(1.0, parseFloat(val) || 0.7));
      if (this.wasmGain && this.audioCtx) {
        this.wasmGain.gain.setTargetAtTime(this.wasmVolume, this.audioCtx.currentTime, 0.05);
      }
    }

    getWasmVolume() {
      return this.wasmVolume;
    }

    setPhysicalVolume(val) {
      this.physicalVolume = Math.max(0.0, Math.min(1.0, parseFloat(val) || 0.8));
      if (this.physicalGain && this.audioCtx) {
        this.physicalGain.gain.setTargetAtTime(this.physicalVolume, this.audioCtx.currentTime, 0.05);
      }
    }

    getPhysicalVolume() {
      return this.physicalVolume;
    }

    // Final output bus (compressor + limiter) that external engines must feed instead of the destination
    getOutputNode() {
      this.initAudioGraph();
      return this.outBus || null;
    }

    // Returns the AudioNode input for external engines (e.g. ambient-engine.js) to route into Space Bus
    getSpaceInputNode() {
      this.initAudioGraph();
      return this.spaceInputGain;
    }

    getAudioContext() {
      this.initAudioGraph();
      return this.audioCtx;
    }

    // Updates lowpass cutoff based on smoothness & soothingness
    updateSoothingFilter() {
      if (!this.soothingFilter || !this.audioCtx) return;
      // Cutoff drops from 8500 Hz down to 2200 Hz with high soothingness & smoothness
      const cutoff = 8500 - this.soothingness * 4800 - this.smoothness * 1500;
      this.soothingFilter.frequency.setTargetAtTime(Math.max(1600, cutoff), this.audioCtx.currentTime, 0.08);
    }

    updateSpaceKnob() {
      if (!this.reverbGain || !this.delayGain || !this.spaceSend || !this.audioCtx) return;
      const revMix = Math.min(0.75, 0.15 + this.space * 0.55);
      const delMix = Math.min(0.65, 0.08 + this.space * 0.42);
      this.reverbGain.gain.setTargetAtTime(revMix, this.audioCtx.currentTime, 0.08);
      this.delayGain.gain.setTargetAtTime(delMix, this.audioCtx.currentTime, 0.08);
      this.spaceSend.gain.setTargetAtTime(0.3 + this.space * 0.7, this.audioCtx.currentTime, 0.08);
    }

    updateDelayTempo() {
      if (!this.delayLeft || !this.delayRight || !this.audioCtx) return;
      const beatSec = 60 / Math.max(40, this.currentBpm);
      this.delayLeft.delayTime.setTargetAtTime(beatSec * 0.75, this.audioCtx.currentTime, 0.1);
      this.delayRight.delayTime.setTargetAtTime(beatSec, this.audioCtx.currentTime, 0.1);
    }

    // -------------------------------------------------------------------------
    // 4. PHYSICAL PIANO SYNTHESIZER (PianoNode.cpp Faithful Web Audio DSP)
    // -------------------------------------------------------------------------
    triggerManualNote(midi, velocity = 0.72, duration = 2.0) {
      this.initAudioGraph();
      if (this.audioCtx && this.audioCtx.state === 'suspended') {
        this.audioCtx.resume();
      }
      return this.triggerNote(midi, velocity, duration, 'melody');
    }

    triggerNote(midi, velocity = 0.65, duration = 1.8, voiceType = 'melody') {
      this.initAudioGraph();
      if (!this.audioCtx) return;

      const ctx = this.audioCtx;
      const now = ctx.currentTime;
      const register = classifyRegister(midi);
      const noteName = midiToNoteName(midi);
      const f0 = midiToFreq(midi);

      // Enforce active note range window:
      const rangeLimits = this.getNoteRangeLimits();
      if (midi < rangeLimits.min || midi > rangeLimits.max) {
        // Octave transpose back into audible window if manual/chord note
        while (midi < rangeLimits.min && midi <= 96) midi += 12;
        while (midi > rangeLimits.max && midi >= 36) midi -= 12;
      }

      // Voice allocation & stealing if limit exceeded
      this.pruneVoices(now);
      if (this.voicePool.length >= this.maxVoices) {
        const oldest = this.voicePool.shift();
        if (oldest && oldest.envelopeGain) {
          try {
            oldest.envelopeGain.gain.cancelScheduledValues(now);
            oldest.envelopeGain.gain.setValueAtTime(oldest.envelopeGain.gain.value, now);
            oldest.envelopeGain.gain.exponentialRampToValueAtTime(0.0001, now + 0.04);
            setTimeout(() => oldest.stop(now + 0.05), 50);
          } catch (e) {}
        }
      }

      const model = PIANO_MODELS[this.pianoModel] || PIANO_MODELS.felt;

      // Effective physical parameters shaped by smoothness & velocity
      const effHardness = model.hardness * (1.0 - this.smoothness * 0.68) * (0.6 + velocity * 0.8);
      const effAttack = model.attack + this.smoothness * 0.022;
      const hammerCutoff = Math.max(380, model.hammerCutoff * (0.4 + effHardness * 0.9) * (0.6 + velocity * 0.6));

      // Voice master gain and panner
      const voiceGain = ctx.createGain();
      const panner = ctx.createStereoPanner ? ctx.createStereoPanner() : null;
      if (panner) {
        // Acoustic piano key position panning (-0.6 at A0 to +0.6 at C8)
        const pan = Math.max(-0.65, Math.min(0.65, ((midi - 21) / 87 - 0.5) * 1.3));
        panner.pan.setValueAtTime(pan, now);
        voiceGain.connect(panner);
        panner.connect(this.soundboardInput);
      } else {
        voiceGain.connect(this.soundboardInput);
      }

      // Nonlinear Chaigne Felt Hammer Exciter Filter
      const hammerFilter = ctx.createBiquadFilter();
      hammerFilter.type = 'lowpass';
      hammerFilter.frequency.setValueAtTime(hammerCutoff, now);
      hammerFilter.Q.setValueAtTime(0.8, now);
      hammerFilter.connect(voiceGain);

      // Inharmonicity coefficient B (Fletcher / PianoNode.cpp line 84)
      const baseB = Math.min(0.028, Math.max(0.00012, 0.0003 * Math.pow(2, (1.2 * (midi - 60)) / 12))) * 2.0 * model.stiffness;

      // T60 fundamental sustain duration from PianoNode.cpp
      const t60 = 22.0 * Math.pow(2, -(midi - 21) / 22.0) * (0.4 + model.decay * 0.8) * (0.6 + this.soothingness * 0.5);

      // Legato damper release extension if smoothness > 0.65
      const legatoExtension = this.smoothness > 0.65 ? (this.smoothness - 0.65) * 1.6 : 0.0;
      const releaseTime = Math.max(0.3, (1.2 + legatoExtension) * (model.decay || 0.7));

      const activeNodes = [];

      if (!model.isElectric) {
        // --- Acoustic 3-Partial Detuned Stiff-String Waveguide ---
        // Partial 1: Center fundamental
        const f1 = f0 * Math.sqrt(1 + baseB);
        const osc1 = ctx.createOscillator();
        osc1.type = 'sine';
        osc1.frequency.setValueAtTime(f1, now);

        // Partial 2: Octave harmonic detuned -detune cents
        const detuneCents = model.unisonDetune || 2.0;
        const f2 = 2 * f0 * Math.sqrt(1 + 4 * baseB) * Math.pow(2, -detuneCents / 1200);
        const osc2 = ctx.createOscillator();
        osc2.type = 'sine';
        osc2.frequency.setValueAtTime(f2, now);

        // Partial 3: 3rd harmonic detuned +detune cents
        const f3 = 3 * f0 * Math.sqrt(1 + 9 * baseB) * Math.pow(2, +detuneCents / 1200);
        const osc3 = ctx.createOscillator();
        osc3.type = 'sine';
        osc3.frequency.setValueAtTime(f3, now);

        // Partial mixing gains
        const g1 = ctx.createGain();
        const g2 = ctx.createGain();
        const g3 = ctx.createGain();

        // High partial damping factor
        const toneDamp = Math.max(0.1, model.tone * (1.1 - this.smoothness * 0.4));
        g1.gain.setValueAtTime(0.75, now);
        g2.gain.setValueAtTime(0.32 * toneDamp, now);
        g3.gain.setValueAtTime(0.18 * toneDamp * toneDamp, now);

        // Partial decay envelopes (higher partials die faster: 1/f)
        const d1 = t60 * 0.6;
        const d2 = t60 * 0.28;
        const d3 = t60 * 0.14;

        g1.gain.exponentialRampToValueAtTime(0.001, now + Math.max(0.5, d1));
        g2.gain.exponentialRampToValueAtTime(0.0005, now + Math.max(0.3, d2));
        g3.gain.exponentialRampToValueAtTime(0.0001, now + Math.max(0.15, d3));

        osc1.connect(g1); g1.connect(hammerFilter);
        osc2.connect(g2); g2.connect(hammerFilter);
        osc3.connect(g3); g3.connect(hammerFilter);

        osc1.start(now); osc2.start(now); osc3.start(now);
        activeNodes.push(osc1, osc2, osc3);

        // Sub-harmonic anchor for deep grounded bass (MIDI 21 to 40)
        if (midi < 42) {
          const subOsc = ctx.createOscillator();
          subOsc.type = 'triangle';
          subOsc.frequency.setValueAtTime(f0, now);
          const subGain = ctx.createGain();
          subGain.gain.setValueAtTime(0.42 * (1.0 + (this.taste.featW[15] || 0) * 0.3), now);
          subGain.gain.exponentialRampToValueAtTime(0.001, now + t60 * 0.8);
          subOsc.connect(subGain);
          subGain.connect(hammerFilter);
          subOsc.start(now);
          activeNodes.push(subOsc);
        }
      } else {
        // --- Rhodes / Electric Piano Tine & FM Bell Model ---
        const carrier = ctx.createOscillator();
        carrier.type = 'sine';
        carrier.frequency.setValueAtTime(f0, now);

        // Bell tine overtone (inharmonic tine frequency ~ 3.98 * f0)
        const tine = ctx.createOscillator();
        tine.type = 'sine';
        tine.frequency.setValueAtTime(f0 * 3.98, now);

        const tineGain = ctx.createGain();
        tineGain.gain.setValueAtTime(0.55 * (model.tineBell || 0.6), now);
        tineGain.gain.exponentialRampToValueAtTime(0.0005, now + 0.12);

        tine.connect(tineGain);
        tineGain.connect(hammerFilter);
        carrier.connect(hammerFilter);

        carrier.start(now);
        tine.start(now);
        activeNodes.push(carrier, tine);
      }

      // Voice Master Envelope (Attack, Sustain, Release)
      const peakAmp = Math.max(0.05, Math.min(1.0, velocity * 0.72));
      voiceGain.gain.setValueAtTime(0.0001, now);
      voiceGain.gain.exponentialRampToValueAtTime(peakAmp, now + effAttack);
      const sustainTime = Math.max(0.2, duration);
      voiceGain.gain.setValueAtTime(peakAmp * 0.7, now + sustainTime);
      voiceGain.gain.exponentialRampToValueAtTime(0.0001, now + sustainTime + releaseTime);

      const stopTime = now + sustainTime + releaseTime + 0.05;
      const voiceRecord = {
        midi,
        voiceGain,
        envelopeGain: voiceGain,
        stopTime,
        stop: (t) => {
          activeNodes.forEach((n) => {
            try { n.stop(t); } catch (e) {}
          });
        }
      };
      this.voicePool.push(voiceRecord);

      // Auto-stop oscillators
      setTimeout(() => {
        voiceRecord.stop(ctx.currentTime);
      }, Math.max(100, (sustainTime + releaseTime + 0.1) * 1000));

      // Dispatch onNote callback for 3D world & 88-key piano roll visualizer
      this.dispatchNoteEvent({
        midi,
        freq: f0,
        velocity,
        duration: sustainTime,
        register,
        noteName,
        voiceType,
        time: now
      });

      return voiceRecord;
    }

    pruneVoices(now) {
      this.voicePool = this.voicePool.filter((v) => v.stopTime > now);
    }

    getNoteRangeLimits() {
      // Maps noteRangeWidth (0..1) to MIDI limits (default 0.88 is A0=21 to C8=108)
      const w = this.noteRangeWidth;
      const min = Math.round(48 - w * 27); // 48 down to 21 (A0)
      const max = Math.round(72 + w * 36); // 72 up to 108 (C8)
      return { min: Math.max(21, min), max: Math.min(108, max) };
    }

    // -------------------------------------------------------------------------
    // 5. CONTINUOUS C++ WASM PIANONODE STREAMER (gen-worker.js)
    // -------------------------------------------------------------------------
    initWasmWorker() {
      if (this.worker || typeof Worker === 'undefined') return;

      const workerPaths = [
        '../gen/gen-worker.js?v=9',
        './gen/gen-worker.js?v=9',
        '/gen/gen-worker.js?v=9'
      ];

      const trySpawn = (idx) => {
        if (idx >= workerPaths.length) {
          this.wasmStatus = 'disabled';
          console.warn('InfinitePianoEngine: gen-worker.js unavailable; physical engine active.');
          return;
        }

        try {
          const w = new Worker(workerPaths[idx]);
          this.wasmStatus = 'loading';

          w.onmessage = (e) => {
            this.handleWasmMessage(e.data);
          };

          w.onerror = (err) => {
            w.terminate();
            trySpawn(idx + 1);
          };

          this.worker = w;
          // Send info query to worker
          w.postMessage({ cmd: 'info', type: 'info', id: 'init' });
        } catch (err) {
          trySpawn(idx + 1);
        }
      };

      trySpawn(0);
    }

    handleWasmMessage(data) {
      if (!data) return;

      // Init info response
      if (data.styles && data.scales) {
        this.wasmStyles = data.styles;
        this.wasmScales = data.scales;
        this.wasmStatus = 'ready';
        this.dispatchWasmUpdate({
          status: 'ready',
          styles: this.wasmStyles,
          scales: this.wasmScales,
          label: 'WASM Ready',
          why: 'C++ PianoNode compiled to WebAssembly'
        });

        // Trigger first background phrase render if playing
        if (this.isPlayingState) {
          this.requestNextWasmPhrase();
        }
        return;
      }

      // Rendered phrase PCM buffer from gen-worker.js
      if (data.pcm || (data.left && data.right)) {
        this.wasmPending = false;
        this.wasmStatus = 'playing';

        const frames = data.frames || data.n || (data.pcm ? data.pcm.length / 2 : data.left.length);
        const rate = data.rate || data.sr || 48000;

        let leftF32, rightF32;
        if (data.left && data.right) {
          leftF32 = data.left;
          rightF32 = data.right;
        } else if (data.pcm) {
          leftF32 = new Float32Array(frames);
          rightF32 = new Float32Array(frames);
          for (let i = 0; i < frames; i++) {
            leftF32[i] = data.pcm[i * 2];
            rightF32[i] = data.pcm[i * 2 + 1];
          }
        }

        // Build AudioBuffer
        if (this.audioCtx && leftF32 && rightF32) {
          const audioBuffer = this.audioCtx.createBuffer(2, frames, rate);
          audioBuffer.copyToChannel(leftF32, 0);
          audioBuffer.copyToChannel(rightF32, 1);

          this.lastPatch = data.patch || this.lastPatch;
          this.lastLabel = data.label || 'Infinite PianoNode';
          this.lastWhy = data.why || data.label || 'Live C++ WASM Generative Stem';
          this.lastAttempt = data.attempt || 1;

          this.dispatchWasmUpdate({
            status: 'phrase_ready',
            label: this.lastLabel,
            patch: this.lastPatch,
            bpm: data.bpm || this.currentBpm,
            attempt: this.lastAttempt,
            why: this.lastWhy
          });

          // Schedule continuous seamless playback with 1.5-second equal-power crossfade
          this.scheduleWasmPhrase(audioBuffer, data.bpm);
        }
      }
    }

    requestNextWasmPhrase() {
      if (!this.worker || this.wasmStatus === 'loading' || this.wasmPending) return;
      this.wasmPending = true;

      // Select part: mostly 18 (Piano), occasionally 2 (Pads) or 9 (Keys)
      const partRoll = Math.random();
      const part = partRoll < 0.76 ? 18 : partRoll < 0.90 ? 2 : 9;

      // Style index from available styles
      const styleIdx = Math.floor(Math.random() * Math.max(1, this.wasmStyles.length));
      const seed = Math.floor(Math.random() * 1000000);

      // Shape parameters mapped from Smoothness, Soothingness, Space & Taste
      const tastePhi = this.taste.featW;
      const tone = (1.0 - this.smoothness) * 0.8 - 0.4 + (tastePhi[7] || 0) * 0.25;
      const energy = (1.0 - this.soothingness) * 1.2 - 0.65 + (tastePhi[0] || 0) * 0.25;
      const spaceVal = this.space * 1.6 - 0.8;
      const mood = this.soothingness * 1.2 - 0.2 + (tastePhi[13] || 0) * 0.25;
      const variation = 0.38 + (tastePhi[12] || 0) * 0.2;
      const bpm = this.currentBpm;

      const scaleIdx = this.getWasmScaleIndex(this.scale);

      const msg = {
        cmd: 'run',
        type: 'run',
        id: ++this.wasmJobId,
        style: styleIdx,
        styleIdx,
        part,
        seed,
        bars: 4,
        key: this.key,
        scale: scaleIdx,
        tone,
        energy,
        space: spaceVal,
        mood,
        variation,
        bpm,
        pinned: 1
      };

      this.worker.postMessage(msg);
    }

    getWasmScaleIndex(scaleName) {
      const idx = this.wasmScales.findIndex((s) => s.toLowerCase() === scaleName.toLowerCase());
      return idx >= 0 ? idx : 0;
    }

    // Seamless 1.5-second equal-power crossfader at phrase boundaries
    scheduleWasmPhrase(audioBuffer, bpm) {
      if (!this.audioCtx || !this.isPlayingState) return;

      const ctx = this.audioCtx;
      const now = ctx.currentTime;
      const duration = audioBuffer.duration;
      const crossfadeSec = 1.5;

      let startTime = this.wasmNextStartTime;
      if (startTime < now) {
        startTime = now + 0.05;
      }

      const source = ctx.createBufferSource();
      source.buffer = audioBuffer;

      const phraseGain = ctx.createGain();
      phraseGain.gain.setValueAtTime(0.0001, startTime);

      // Fade in over crossfadeSec
      phraseGain.gain.linearRampToValueAtTime(1.0, startTime + crossfadeSec);

      // Sustain until phrase boundary
      const fadeOutStart = Math.max(startTime + crossfadeSec, startTime + duration - crossfadeSec);
      phraseGain.gain.setValueAtTime(1.0, fadeOutStart);

      // Fade out over crossfadeSec
      phraseGain.gain.linearRampToValueAtTime(0.0001, startTime + duration);

      source.connect(phraseGain);
      phraseGain.connect(this.wasmGain);

      source.start(startTime);
      source.stop(startTime + duration + 0.1);

      // Next phrase starts exactly when this phrase begins fading out!
      this.wasmNextStartTime = fadeOutStart;

      // Schedule next WASM render prefetch when current phrase has 3 seconds left
      const lookaheadPrefetch = Math.max(100, (fadeOutStart - now - 2.5) * 1000);
      setTimeout(() => {
        if (this.isPlayingState) {
          this.requestNextWasmPhrase();
        }
      }, lookaheadPrefetch);
    }

    getCurrentPatchText() {
      if (this.lastPatch && this.lastPatch.length > 10) return this.lastPatch;
      return [
        'infinite-patch 1',
        '# Generated by Infinite Piano Symphony & Taste Learning Engine',
        `node 0 PianoNode model=${this.pianoModel} hardness=${(1 - this.smoothness * 0.75).toFixed(2)} tone=${(1 - this.soothingness * 0.5).toFixed(2)}`,
        `node 1 ReverbNode decay=${this.spaceParams.reverbDecay} mix=${this.spaceParams.reverbMix}`,
        `node 2 TapeDelayNode time=${this.spaceParams.delayTime} mix=${this.spaceParams.delayMix}`,
        'connect 0 1',
        'connect 1 2',
        'out 2'
      ].join('\n');
    }

    downloadWasmPatch() {
      const text = this.getCurrentPatchText();
      if (typeof Blob === 'undefined' || typeof document === 'undefined') return;
      const blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `infinite-nocturne-piano-${Date.now()}.inf`;
      document.body.appendChild(a);
      a.click();
      a.remove();
      URL.revokeObjectURL(url);
    }

    // -------------------------------------------------------------------------
    // 6. GENERATIVE COUNTERPOINT ENGINE (3 Musical Strata across A0–C8)
    // -------------------------------------------------------------------------
    startGenerativeScheduler() {
      if (this.schedulerTimer) return;
      this.nextBarTime = this.audioCtx ? this.audioCtx.currentTime + 0.1 : 0.1;

      this.schedulerTimer = setInterval(() => {
        if (!this.isPlayingState || !this.audioCtx) return;
        this.schedulerTick();
      }, this.lookaheadIntervalMs);

      // 15-second dwell reinforcement timer (+0.25)
      this.dwellTimer = setInterval(() => {
        if (this.isPlayingState) {
          this.listeningSeconds += 15;
          this.taste.totalListeningSec += 15;
          this.taste.updateFeatureWeights(this.getCurrentTasteVector(), 0.25, '15s_dwell');
          this.dispatchTasteUpdate();
        }
      }, 15000);
    }

    stopGenerativeScheduler() {
      if (this.schedulerTimer) {
        clearInterval(this.schedulerTimer);
        this.schedulerTimer = null;
      }
      if (this.dwellTimer) {
        clearInterval(this.dwellTimer);
        this.dwellTimer = null;
      }
    }

    schedulerTick() {
      const now = this.audioCtx.currentTime;
      const beatSec = 60 / Math.max(40, this.currentBpm);
      const barSec = beatSec * 4;

      while (this.nextBarTime < now + this.scheduleLookaheadSec) {
        this.scheduleBar(this.nextBarTime, beatSec);
        this.nextBarTime += barSec;
        this.currentBarIndex++;
        if (this.currentBarIndex % 4 === 0) {
          this.phraseIndex++;
        }
      }
    }

    scheduleBar(barStartTime, beatSec) {
      const chordList = this.currentProgression.chords;
      const chordIndex = this.currentBarIndex % chordList.length;
      const chord = chordList[chordIndex];

      const keyRoot = this.key;
      const chordRootMidi = 48 + keyRoot + chord.rootDegree;
      const chordName = `${NOTE_NAMES[(keyRoot + chord.rootDegree) % 12]}${chord.chordName.slice(1)}`;

      // Notify Bar Change callback
      this.dispatchBarChange({
        barIndex: this.currentBarIndex,
        phraseIndex: this.phraseIndex,
        chordName,
        romanNumeral: chord.roman,
        keyName: NOTE_NAMES[this.key],
        scaleName: this.scale,
        bpm: this.currentBpm,
        wasmActive: this.wasmStatus === 'playing',
        wasmLabel: this.lastLabel
      });

      // --- STRATUM 1: DEEP GROUNDED BASS (MIDI 24–47 / C1–B2) ---
      this.scheduleDeepBass(barStartTime, chord, keyRoot, beatSec);

      // --- STRATUM 2: HARMONIC INNER VOICINGS (MIDI 48–71 / C3–B4) ---
      this.scheduleHarmonicInnerVoicings(barStartTime, chord, keyRoot, beatSec);

      // --- STRATUM 3: MELODIC CANOPY & HIGH SPARKLE (MIDI 72–98 / C5–D7) ---
      this.scheduleMelodicCanopy(barStartTime, chord, keyRoot, beatSec);
    }

    // Stratum 1: Deep Grounded Bass
    scheduleDeepBass(barStartTime, chord, keyRoot, beatSec) {
      let bassRoot = 24 + ((keyRoot + chord.rootDegree) % 12);
      if (bassRoot > 36) bassRoot -= 12;

      if (this.noteRangeWidth > 0.8 && bassRoot > 28 && Math.random() < 0.45) {
        bassRoot -= 12;
      }

      const vel = 0.58 + (this.taste.featW[15] || 0) * 0.12 + Math.random() * 0.08;
      const dur = beatSec * 3.8;

      const delayOffset = Math.max(0, barStartTime - this.audioCtx.currentTime);
      setTimeout(() => {
        if (!this.isPlayingState) return;
        this.triggerNote(bassRoot, vel, dur, 'bass');
      }, delayOffset * 1000);

      // Grounding weight: add 5th (7 semitones)
      if (this.taste.featW[15] > -0.2 || Math.random() < 0.68) {
        const fifthNote = bassRoot + 7;
        const fifthTime = barStartTime + beatSec * 2.0;
        const fifthDelay = Math.max(0, fifthTime - this.audioCtx.currentTime);
        setTimeout(() => {
          if (!this.isPlayingState) return;
          this.triggerNote(fifthNote, vel * 0.82, beatSec * 1.8, 'bass');
        }, fifthDelay * 1000);
      }
    }

    // Stratum 2: Harmonic Inner Voicings
    scheduleHarmonicInnerVoicings(barStartTime, chord, keyRoot, beatSec) {
      const intervals = chord.intervals || [0, 4, 7, 11];
      const rootBase = 48 + ((keyRoot + chord.rootDegree) % 12);

      const chordNotes = intervals.map((iv) => {
        let m = rootBase + iv;
        while (m < 48) m += 12;
        while (m > 71) m -= 12;
        return m;
      }).sort((a, b) => a - b);

      // Humanized roll / strum micro-delay
      const rollSpread = 0.015 + this.smoothness * 0.035;

      // Play rolled chord on Beat 1
      chordNotes.forEach((m, idx) => {
        const strumOffset = idx * rollSpread;
        const noteTime = barStartTime + strumOffset;
        const delayOffset = Math.max(0, noteTime - this.audioCtx.currentTime);
        const vel = 0.42 + Math.random() * 0.12 - idx * 0.02;
        const dur = beatSec * 2.2;

        setTimeout(() => {
          if (!this.isPlayingState) return;
          this.triggerNote(m, vel, dur, 'chord');
        }, delayOffset * 1000);
      });

      // Flowing broken arpeggio on Beat 3
      if (Math.random() < 0.70) {
        const arpNote = chordNotes[chordNotes.length - 1];
        const noteTime = barStartTime + beatSec * 2.0;
        const delayOffset = Math.max(0, noteTime - this.audioCtx.currentTime);
        setTimeout(() => {
          if (!this.isPlayingState) return;
          this.triggerNote(arpNote, 0.45, beatSec * 1.6, 'chord');
        }, delayOffset * 1000);
      }
    }

    // Stratum 3: Melodic Canopy & High Sparkle
    scheduleMelodicCanopy(barStartTime, chord, keyRoot, beatSec) {
      const restProb = 0.22 + this.soothingness * 0.15;
      if (Math.random() < restProb) {
        return;
      }

      const scaleDegrees = SCALES[this.scale] || SCALES.dorian;
      const baseOctave = 72;

      const numNotes = Math.random() < 0.5 ? 2 : 3;
      const beats = numNotes === 2 ? [1.0, 3.0] : [0.5, 2.0, 3.5];

      for (let i = 0; i < numNotes; i++) {
        const beatOffset = beats[i] * beatSec;
        const noteTime = barStartTime + beatOffset;

        const stepIdx = Math.floor(Math.random() * scaleDegrees.length);
        let midi = baseOctave + ((keyRoot + scaleDegrees[stepIdx]) % 12);
        if (Math.random() < 0.35) midi += 12;

        while (midi < 72) midi += 12;
        while (midi > 98) midi -= 12;

        const vel = 0.46 + Math.random() * 0.14;
        const dur = beatSec * 1.2;

        const delayOffset = Math.max(0, noteTime - this.audioCtx.currentTime);
        setTimeout(() => {
          if (!this.isPlayingState) return;
          this.triggerNote(midi, vel, dur, 'melody');
        }, delayOffset * 1000);
      }

      // Upper sparkle bell tone (MIDI 84–106)
      const sparkleChance = 0.35 + (this.taste.featW[14] || 0) * 0.2;
      if (Math.random() < sparkleChance) {
        const sparkleDegree = scaleDegrees[Math.floor(Math.random() * scaleDegrees.length)];
        let sparkleMidi = 84 + ((keyRoot + sparkleDegree) % 12);
        if (Math.random() < 0.4) sparkleMidi += 12;
        if (sparkleMidi > 105) sparkleMidi -= 12;

        const sparkleTime = barStartTime + beatSec * 3.25;
        const delayOffset = Math.max(0, sparkleTime - this.audioCtx.currentTime);
        setTimeout(() => {
          if (!this.isPlayingState) return;
          this.triggerNote(sparkleMidi, 0.44, beatSec * 2.1, 'sparkle');
        }, delayOffset * 1000);
      }
    }

    // -------------------------------------------------------------------------
    // 7. PUBLIC INTERACTION & EVOLUTION CONTROLS
    // -------------------------------------------------------------------------
    async start() {
      this.initAudioGraph();
      if (!this.audioCtx) return;

      if (this.audioCtx.state === 'suspended') {
        await this.audioCtx.resume();
      }

      this.isPlayingState = true;
      this.isStarted = true;

      // Start C++ WASM Phrase Pipeline
      this.initWasmWorker();

      // Start Generative Counterpoint Scheduler
      this.startGenerativeScheduler();
    }

    pause() {
      this.isPlayingState = false;
      this.stopGenerativeScheduler();
      if (this.audioCtx && this.audioCtx.state === 'running') {
        this.audioCtx.suspend();
      }
    }

    stop() {
      this.pause();
    }

    togglePlay() {
      if (this.isPlayingState) {
        this.pause();
      } else {
        this.start();
      }
      return this.isPlayingState;
    }

    isPlaying() {
      return this.isPlayingState;
    }

    // Called when user clicks "Love" / "Keep" or skips
    recordFeedback(reward, reason = 'feedback') {
      const phi = this.getCurrentTasteVector();
      this.taste.updateFeatureWeights(phi, reward, reason);
      this.taste.recordBetaFeedback(this.key, this.scale, this.currentProgression.id, this.pianoModel, reward);
      this.dispatchTasteUpdate();
    }

    // Transitions to a new learned phrase / progression / key gracefully
    evolveNextPhrase() {
      this.phraseIndex++;
      this.key = this.taste.samplePreferredKey();
      this.scale = this.taste.samplePreferredScale();

      const candidates = PROGRESSION_CORPUS.filter((p) => p.scale === this.scale);
      if (candidates.length > 0) {
        this.currentProgression = candidates[Math.floor(Math.random() * candidates.length)];
      } else {
        this.currentProgression = PROGRESSION_CORPUS[Math.floor(Math.random() * PROGRESSION_CORPUS.length)];
        this.scale = this.currentProgression.scale;
      }

      // Next timbre model
      this.pianoModel = this.taste.samplePreferredModel();

      // Request next WASM phrase with new learned tonality
      this.requestNextWasmPhrase();

      this.recordFeedback(0.2, 'evolve_phrase');
    }

    getCurrentTasteVector() {
      const v = new Float32Array(16);
      v[0] = 0.5 - this.soothingness * 0.3; // density
      v[1] = 0.3;                          // syncopation
      v[2] = 0.25;                         // swing
      v[3] = 0.7;                          // bassPresence
      v[4] = 0.65;                         // chordRichness
      v[5] = 0.35;                         // melodicLeap
      v[6] = this.noteRangeWidth;          // registerSpread
      v[7] = this.smoothness * 0.8;        // harmonicWarmth
      v[8] = this.smoothness;              // smoothnessBias
      v[9] = this.soothingness;            // soothingBias
      v[10] = this.space;                  // spaceBias
      v[11] = 0.7;                         // tempoCalmness
      v[12] = 0.55;                        // motifRepetition
      v[13] = 0.6;                         // modalColor
      v[14] = 0.45;                        // upperSparkle
      v[15] = 0.75;                        // groundingWeight
      return v;
    }

    applyTastePreferences() {
      if (this.taste && this.taste.phrasesLearned > 0) {
        this.key = this.taste.samplePreferredKey();
        this.scale = this.taste.samplePreferredScale();
        this.pianoModel = this.taste.samplePreferredModel();
      }
    }

    getTasteSnapshot() {
      return this.taste.getSnapshot({
        smoothness: this.smoothness,
        soothingness: this.soothingness,
        space: this.space,
        noteRangeWidth: this.noteRangeWidth,
        grounding: 0.85
      });
    }

    resetTaste() {
      this.taste.reset();
      this.dispatchTasteUpdate();
    }

    // -------------------------------------------------------------------------
    // 8. TELEMETRY & ANALYSER
    // -------------------------------------------------------------------------
    getAnalyserData() {
      if (!this.analyser) {
        return {
          waveform: new Float32Array(256),
          spectrum: new Uint8Array(256),
          rms: 0,
          bassEnergy: 0,
          midEnergy: 0,
          trebleEnergy: 0
        };
      }

      const bins = this.analyser.frequencyBinCount;
      const freqData = new Uint8Array(bins);
      const timeData = new Float32Array(bins);

      this.analyser.getByteFrequencyData(freqData);
      this.analyser.getFloatTimeDomainData(timeData);

      // Compute RMS
      let sumSq = 0;
      for (let i = 0; i < timeData.length; i++) {
        sumSq += timeData[i] * timeData[i];
      }
      const rms = Math.min(1.0, Math.sqrt(sumSq / timeData.length) * 2.2);

      // Compute Bass (20–250Hz), Mid (250–2500Hz), Treble (2500–16000Hz)
      const nyquist = (this.audioCtx ? this.audioCtx.sampleRate : 48000) / 2;
      const hzPerBin = nyquist / bins;

      let bassSum = 0, bassCount = 0;
      let midSum = 0, midCount = 0;
      let trebleSum = 0, trebleCount = 0;

      for (let i = 0; i < bins; i++) {
        const hz = i * hzPerBin;
        const val = freqData[i] / 255;
        if (hz >= 20 && hz < 250) {
          bassSum += val; bassCount++;
        } else if (hz >= 250 && hz < 2500) {
          midSum += val; midCount++;
        } else if (hz >= 2500 && hz < 16000) {
          trebleSum += val; trebleCount++;
        }
      }

      return {
        waveform: timeData,
        spectrum: freqData,
        rms,
        bassEnergy: bassCount ? bassSum / bassCount : 0,
        midEnergy: midCount ? midSum / midCount : 0,
        trebleEnergy: trebleCount ? trebleSum / trebleCount : 0
      };
    }

    // -------------------------------------------------------------------------
    // 9. EVENT REGISTRATION & DISPATCH
    // -------------------------------------------------------------------------
    onNote(cb) {
      if (typeof cb === 'function') {
        this.callbacks.note.push(cb);
        return () => {
          this.callbacks.note = this.callbacks.note.filter((f) => f !== cb);
        };
      }
    }

    onBarChange(cb) {
      if (typeof cb === 'function') {
        this.callbacks.bar.push(cb);
        return () => {
          this.callbacks.bar = this.callbacks.bar.filter((f) => f !== cb);
        };
      }
    }

    onWasmUpdate(cb) {
      if (typeof cb === 'function') {
        this.callbacks.wasm.push(cb);
        return () => {
          this.callbacks.wasm = this.callbacks.wasm.filter((f) => f !== cb);
        };
      }
    }

    onTasteUpdate(cb) {
      if (typeof cb === 'function') {
        this.callbacks.taste.push(cb);
        return () => {
          this.callbacks.taste = this.callbacks.taste.filter((f) => f !== cb);
        };
      }
    }

    dispatchNoteEvent(evt) {
      for (let i = 0; i < this.callbacks.note.length; i++) {
        try { this.callbacks.note[i](evt); } catch (e) {}
      }
    }

    dispatchBarChange(evt) {
      for (let i = 0; i < this.callbacks.bar.length; i++) {
        try { this.callbacks.bar[i](evt); } catch (e) {}
      }
    }

    dispatchWasmUpdate(evt) {
      for (let i = 0; i < this.callbacks.wasm.length; i++) {
        try { this.callbacks.wasm[i](evt); } catch (e) {}
      }
    }

    dispatchTasteUpdate() {
      const snap = this.getTasteSnapshot();
      for (let i = 0; i < this.callbacks.taste.length; i++) {
        try { this.callbacks.taste[i](snap); } catch (e) {}
      }
    }
  }

  // ===========================================================================
  // 10. GLOBAL EXPORT & SINGLETON INTEGRATION
  // ===========================================================================
  // Allow instantiation as `new InfinitePianoEngine()`, calling `InfinitePianoEngine()`,
  // or accessing static singleton methods on `window.InfinitePianoEngine`
  const defaultInstance = new InfinitePianoEngine();

  // Export constructor function with static delegation to defaultInstance
  function EngineWrapper(options) {
    if (this instanceof EngineWrapper || this instanceof InfinitePianoEngine) {
      return new InfinitePianoEngine(options);
    }
    return defaultInstance;
  }

  // Copy prototype and static helper methods
  EngineWrapper.prototype = InfinitePianoEngine.prototype;
  EngineWrapper.Engine = InfinitePianoEngine;
  EngineWrapper.instance = defaultInstance;

  // Delegate all instance methods statically onto EngineWrapper
  const protoProps = Object.getOwnPropertyNames(InfinitePianoEngine.prototype);
  protoProps.forEach((prop) => {
    if (prop !== 'constructor' && typeof InfinitePianoEngine.prototype[prop] === 'function') {
      EngineWrapper[prop] = function (...args) {
        return defaultInstance[prop](...args);
      };
    }
  });

  // Global exposure
  if (typeof global !== 'undefined') {
    global.InfinitePianoEngine = EngineWrapper;
  }

})(typeof window !== 'undefined' ? window : (typeof globalThis !== 'undefined' ? globalThis : this));
