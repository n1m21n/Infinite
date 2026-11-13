/**
 * Infinite Ambient Audio Engine & Infinite Sampler
 * Part of the Infinite Piano Symphony & 3D Night Experience
 * 
 * Architecture:
 * - Hybrid Procedural DSP (0ms instant response) + Infinite Sampler (.mp3 seamless loopers)
 * - 12 High-fidelity ambient channels covering all 11 sound categories:
 *   1. rain: Midnight Rain on leaves and windowpane
 *   2. vinyl: Warm vintage vinyl crackle & analog needle groove
 *   3. pebbles: Ocean tide washing over smooth shore pebbles
 *   4. waterfall: Peaceful forest waterfall cascade into deep pool
 *   5. thunder: Distant rolling midnight thunder rumbles (triggers lightning callback)
 *   6. noise: Velvet / Pink / Brown / White soothing noise bed (dynamic noise types)
 *   7. crickets: Peaceful night crickets, summer cicadas, and distant owl
 *   8. leaves: Gentle wind through pine trees and rustling autumn leaves
 *   9. fire: Crackling campfire logs and glowing embers
 *   10. water_drops: Babbling forest brook, mossy rocks, and cave water droplets
 *   11. cafe: Cozy late-night cafe murmur, soft ceramic cup clinks, library tone
 *   12. soothing: Slow ocean waves, Tibetan singing bowls, and resonant wind chimes
 * - Infinite Space Bus: Lush algorithmic convolution reverb & analog tape delay
 * - Real-time Weather State telemetry for 3D Minecraft Night Visualizer
 */

(function (root, factory) {
  if (typeof define === 'function' && define.amd) {
    define([], factory);
  } else if (typeof module === 'object' && module.exports) {
    module.exports = factory();
  } else {
    root.InfiniteAmbientEngine = factory();
  }
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  // Channel catalog & default acoustic parameters
  var CHANNEL_DEFS = {
    rain: {
      id: 'rain',
      name: 'Midnight Rain',
      category: 'Rain Sounds',
      file: 'rain.mp3',
      defaultTone: 0.75,
      defaultSend: 0.35,
      weatherCategory: 'rain'
    },
    vinyl: {
      id: 'vinyl',
      name: 'Vintage Vinyl',
      category: 'Vinyl Sounds',
      file: 'vinyl.mp3',
      defaultTone: 0.65,
      defaultSend: 0.15,
      weatherCategory: null
    },
    pebbles: {
      id: 'pebbles',
      name: 'Shore Pebbles',
      category: 'Pebble Sounds',
      file: 'pebbles.mp3',
      defaultTone: 0.70,
      defaultSend: 0.40,
      weatherCategory: 'water'
    },
    waterfall: {
      id: 'waterfall',
      name: 'Forest Waterfall',
      category: 'Waterfall Sounds',
      file: 'waterfall.mp3',
      defaultTone: 0.60,
      defaultSend: 0.50,
      weatherCategory: 'water'
    },
    thunder: {
      id: 'thunder',
      name: 'Rolling Thunder',
      category: 'Thunder Sounds',
      file: 'thunder.mp3',
      defaultTone: 0.50,
      defaultSend: 0.60,
      weatherCategory: 'thunder'
    },
    noise: {
      id: 'noise',
      name: 'Velvet Noise',
      category: 'White / Pink / Brown / Velvet Noise',
      file: 'noise.mp3',
      defaultTone: 0.55,
      defaultSend: 0.20,
      weatherCategory: null
    },
    crickets: {
      id: 'crickets',
      name: 'Night Crickets',
      category: 'Night / Crickets',
      file: 'crickets.mp3',
      defaultTone: 0.80,
      defaultSend: 0.30,
      weatherCategory: 'crickets'
    },
    leaves: {
      id: 'leaves',
      name: 'Wind & Leaves',
      category: 'Leaves / Nature / Wind / Rustling',
      file: 'leaves.mp3',
      defaultTone: 0.70,
      defaultSend: 0.35,
      weatherCategory: 'wind'
    },
    fire: {
      id: 'fire',
      name: 'Campfire Embers',
      category: 'Campfire, Logs & Embers',
      file: 'fire.mp3',
      defaultTone: 0.70,
      defaultSend: 0.25,
      weatherCategory: 'fire'
    },
    water_drops: {
      id: 'water_drops',
      name: 'Brook & Cave Drops',
      category: 'Stream, Rocks, Trees & Water Drops',
      file: 'water_drops.mp3',
      defaultTone: 0.75,
      defaultSend: 0.55,
      weatherCategory: 'water'
    },
    cafe: {
      id: 'cafe',
      name: 'Late-Night Cafe',
      category: 'Cafe & Quiet Office Ambience',
      file: 'cafe.mp3',
      defaultTone: 0.60,
      defaultSend: 0.25,
      weatherCategory: null
    },
    soothing: {
      id: 'soothing',
      name: 'Singing Bowls & Waves',
      category: 'Singing Bowls, Ocean Waves & Wind Chimes',
      file: 'soothing.mp3',
      defaultTone: 0.85,
      defaultSend: 0.70,
      weatherCategory: 'water'
    }
  };

  // Built-in curated ambient mixes
  var PRESETS = {
    midnight_grove: {
      name: 'Midnight Grove',
      description: 'Crickets, rustling leaves, gentle forest brook and glowing campfire',
      levels: { crickets: 0.75, leaves: 0.60, water_drops: 0.65, fire: 0.30 }
    },
    rainy_vinyl: {
      name: 'Rainy Vinyl',
      description: 'Midnight rain, dusty vinyl needle groove, distant rolling thunder, and warm velvet noise',
      levels: { rain: 0.80, vinyl: 0.70, thunder: 0.55, noise: 0.35 }
    },
    pebble_cove: {
      name: 'Pebble Cove',
      description: 'Ocean waves rolling smooth shore pebbles, distant waterfall, wind chimes, and pine breeze',
      levels: { pebbles: 0.75, waterfall: 0.50, soothing: 0.65, leaves: 0.40 }
    },
    cozy_cafe: {
      name: 'Cozy Cafe',
      description: 'Warm cafe murmur, soft ceramic cup clinks, rain tapping on window, and warm vinyl spin',
      levels: { cafe: 0.80, rain: 0.50, vinyl: 0.60 }
    },
    deep_zen: {
      name: 'Deep Zen',
      description: 'Tibetan singing bowls, resonant wind chimes, cave water droplets, velvet noise, and quiet crickets',
      levels: { soothing: 0.85, water_drops: 0.65, noise: 0.35, crickets: 0.45 }
    },
    clear: {
      name: 'Clear',
      description: 'Mute all ambient channels',
      levels: {}
    }
  };

  // Helper: detect default asset base path
  function getDefaultBasePath() {
    if (typeof window !== 'undefined' && window.INFINITE_AMBIENT_BASE_PATH) {
      return window.INFINITE_AMBIENT_BASE_PATH;
    }
    if (typeof window !== 'undefined' && window.location && window.location.pathname) {
      var p = window.location.pathname;
      if (p.indexOf('/piano/') !== -1) {
        return '../assets/ambient/';
      }
    }
    return 'assets/ambient/';
  }

  // Tone to BiquadFilter frequency mapping (300 Hz .. 18,000 Hz exponential)
  function toneToFrequency(tone) {
    var t = Math.max(0, Math.min(1, tone));
    return 300 * Math.pow(18000 / 300, t);
  }

  /**
   * Procedural Buffer Generators (in-memory WebAudio synthesis for 0ms instant start)
   */
  var ProceduralSynth = {
    generateNoiseBuffer: function (ctx, type, duration) {
      duration = duration || 2.0;
      var sr = ctx.sampleRate;
      var len = Math.floor(sr * duration);
      var buffer = ctx.createBuffer(2, len, sr);
      var left = buffer.getChannelData(0);
      var right = buffer.getChannelData(1);

      type = (type || 'velvet').toLowerCase();

      if (type === 'white') {
        for (var i = 0; i < len; i++) {
          left[i] = (Math.random() * 2 - 1) * 0.5;
          right[i] = (Math.random() * 2 - 1) * 0.5;
        }
      } else if (type === 'pink') {
        // Voss-McCartney pink noise approximation
        var b0_l = 0, b1_l = 0, b2_l = 0, b3_l = 0, b4_l = 0, b5_l = 0, b6_l = 0;
        var b0_r = 0, b1_r = 0, b2_r = 0, b3_r = 0, b4_r = 0, b5_r = 0, b6_r = 0;
        for (var p = 0; p < len; p++) {
          var white_l = Math.random() * 2 - 1;
          b0_l = 0.99886 * b0_l + white_l * 0.0555179;
          b1_l = 0.99332 * b1_l + white_l * 0.0750759;
          b2_l = 0.96900 * b2_l + white_l * 0.1538520;
          b3_l = 0.86650 * b3_l + white_l * 0.3104856;
          b4_l = 0.55000 * b4_l + white_l * 0.5329522;
          b5_l = -0.7616 * b5_l - white_l * 0.0168980;
          left[p] = (b0_l + b1_l + b2_l + b3_l + b4_l + b5_l + b6_l + white_l * 0.5362) * 0.11;
          b6_l = white_l * 0.115926;

          var white_r = Math.random() * 2 - 1;
          b0_r = 0.99886 * b0_r + white_r * 0.0555179;
          b1_r = 0.99332 * b1_r + white_r * 0.0750759;
          b2_r = 0.96900 * b2_r + white_r * 0.1538520;
          b3_r = 0.86650 * b3_r + white_r * 0.3104856;
          b4_r = 0.55000 * b4_r + white_r * 0.5329522;
          b5_r = -0.7616 * b5_r - white_r * 0.0168980;
          right[p] = (b0_r + b1_r + b2_r + b3_r + b4_r + b5_r + b6_r + white_r * 0.5362) * 0.11;
          b6_r = white_r * 0.115926;
        }
      } else if (type === 'brown') {
        var lastOut_l = 0.0;
        var lastOut_r = 0.0;
        for (var b = 0; b < len; b++) {
          var w_l = Math.random() * 2 - 1;
          lastOut_l = (lastOut_l + (0.02 * w_l)) / 1.02;
          left[b] = lastOut_l * 2.8;

          var w_r = Math.random() * 2 - 1;
          lastOut_r = (lastOut_r + (0.02 * w_r)) / 1.02;
          right[b] = lastOut_r * 2.8;
        }
      } else {
        // 'velvet' noise: sparse random bipolar pulses smoothed by gentle decay
        for (var v = 0; v < len; v++) {
          if (Math.random() < 0.035) {
            var sign = Math.random() < 0.5 ? -1 : 1;
            var amp = (0.2 + Math.random() * 0.8) * sign * 0.45;
            left[v] = amp;
            right[v] = amp * (0.8 + Math.random() * 0.4);
          } else {
            left[v] = left[Math.max(0, v - 1)] * 0.88;
            right[v] = right[Math.max(0, v - 1)] * 0.88;
          }
        }
      }
      return buffer;
    },

    generateChannelBuffer: function (ctx, id) {
      var sr = ctx.sampleRate;
      var duration = (id === 'pebbles' || id === 'soothing' || id === 'thunder') ? 3.0 : 2.0;
      var len = Math.floor(sr * duration);
      var buffer = ctx.createBuffer(2, len, sr);
      var left = buffer.getChannelData(0);
      var right = buffer.getChannelData(1);

      if (id === 'noise') {
        return this.generateNoiseBuffer(ctx, 'velvet', duration);
      }

      var i, t, amp, pan, pos, decay;
      if (id === 'rain') {
        // Rain bed + micro-droplets
        for (i = 0; i < len; i++) {
          var w = (Math.random() * 2 - 1) * 0.25;
          left[i] = w;
          right[i] = (Math.random() * 2 - 1) * 0.25;
          if (Math.random() < 0.008) {
            amp = (Math.random() * 0.7 + 0.3);
            left[i] += amp;
            right[i] += amp * (Math.random() * 0.8 + 0.2);
          }
        }
      } else if (id === 'vinyl') {
        // Low rumble + Poisson crackles
        for (i = 0; i < len; i++) {
          t = i / sr;
          var rum = Math.sin(2 * Math.PI * 60 * t) * 0.08 + (Math.random() * 2 - 1) * 0.04;
          left[i] = rum;
          right[i] = rum;
          if (Math.random() < 0.002) {
            amp = (Math.random() * 0.8 + 0.2) * (Math.random() < 0.5 ? -1 : 1);
            left[i] += amp;
            right[i] += amp * 0.7;
          }
        }
      } else if (id === 'pebbles') {
        // Wave surge & receding pebble granulations
        for (i = 0; i < len; i++) {
          t = i / sr;
          var wave = Math.sin(2 * Math.PI * (t / duration)) * 0.5 + 0.5;
          var surf = (Math.random() * 2 - 1) * wave * 0.3;
          left[i] = surf;
          right[i] = surf;
          if (wave > 0.4 && Math.random() < 0.015) {
            amp = Math.random() * 0.5;
            left[i] += amp;
            right[i] += amp * Math.random();
          }
        }
      } else if (id === 'waterfall') {
        // Broad cascade turbulence
        for (i = 0; i < len; i++) {
          left[i] = (Math.random() * 2 - 1) * 0.45;
          right[i] = (Math.random() * 2 - 1) * 0.45;
        }
      } else if (id === 'thunder') {
        // Deep sub-bass rumbles
        var lastT = 0;
        for (i = 0; i < len; i++) {
          t = i / sr;
          var sub = Math.sin(2 * Math.PI * 45 * t) * 0.25 + Math.sin(2 * Math.PI * 68 * t) * 0.15;
          lastT = (lastT + (Math.random() * 2 - 1) * 0.05) / 1.01;
          var thunder = (sub + lastT) * (0.5 + 0.5 * Math.sin(2 * Math.PI * 0.3 * t));
          left[i] = thunder;
          right[i] = thunder * 0.95;
        }
      } else if (id === 'crickets') {
        // Pulsed high cricket chirps
        for (i = 0; i < len; i++) {
          t = i / sr;
          var chirpPulse = Math.sin(2 * Math.PI * 35 * t);
          var gate = chirpPulse > 0.7 ? 1.0 : 0.0;
          var carrier = Math.sin(2 * Math.PI * 4700 * t) * 0.35 * gate;
          left[i] = carrier;
          right[i] = carrier * 0.8;
        }
      } else if (id === 'leaves') {
        // Forest wind & rustling bursts
        for (i = 0; i < len; i++) {
          t = i / sr;
          var wind = (Math.random() * 2 - 1) * (0.2 + 0.15 * Math.sin(2 * Math.PI * 0.5 * t));
          left[i] = wind;
          right[i] = wind;
          if (Math.random() < 0.006) {
            left[i] += Math.random() * 0.4;
            right[i] += Math.random() * 0.4;
          }
        }
      } else if (id === 'fire') {
        // Campfire low roar & wood pops
        for (i = 0; i < len; i++) {
          var flame = (Math.random() * 2 - 1) * 0.15;
          left[i] = flame;
          right[i] = flame;
          if (Math.random() < 0.004) {
            amp = Math.random() * 0.8 + 0.2;
            left[i] += amp;
            right[i] += amp * 0.6;
          }
        }
      } else if (id === 'water_drops') {
        // Bubble chirp drips
        for (i = 0; i < len; i++) {
          left[i] = (Math.random() * 2 - 1) * 0.04;
          right[i] = (Math.random() * 2 - 1) * 0.04;
        }
        var dropTimes = [0.2, 0.7, 1.1, 1.6];
        for (var d = 0; d < dropTimes.length; d++) {
          pos = Math.floor(dropTimes[d] * sr);
          var freq = 600 + d * 350;
          for (var k = 0; k < 1200 && pos + k < len; k++) {
            decay = Math.exp(-k / 250);
            var ping = Math.sin(2 * Math.PI * freq * (k / sr)) * decay * 0.45;
            left[pos + k] += ping;
            right[pos + k] += ping * 0.85;
          }
        }
      } else if (id === 'cafe') {
        // Distant murmur & soft cup clinks
        for (i = 0; i < len; i++) {
          t = i / sr;
          var murmur = (Math.random() * 2 - 1) * 0.12 * (0.6 + 0.4 * Math.sin(2 * Math.PI * 3.0 * t));
          left[i] = murmur;
          right[i] = murmur;
        }
        // Ceramic clink
        pos = Math.floor(1.2 * sr);
        for (var c = 0; c < 1800 && pos + c < len; c++) {
          decay = Math.exp(-c / 350);
          var clink = (Math.sin(2 * Math.PI * 3200 * (c / sr)) + Math.sin(2 * Math.PI * 4600 * (c / sr)) * 0.4) * decay * 0.35;
          left[pos + c] += clink;
          right[pos + c] += clink * 0.5;
        }
      } else if (id === 'soothing') {
        // Tibetan bowl chord + ocean breathing
        for (i = 0; i < len; i++) {
          t = i / sr;
          var bowl = (
            Math.sin(2 * Math.PI * 216.0 * t) * 0.35 * (1 + 0.1 * Math.sin(2 * Math.PI * 1.1 * t)) +
            Math.sin(2 * Math.PI * 583.2 * t) * 0.15 +
            Math.sin(2 * Math.PI * 1080.0 * t) * 0.08
          );
          var sea = Math.sin(2 * Math.PI * (t / duration)) * 0.15;
          left[i] = bowl + sea;
          right[i] = bowl * 0.95 + sea;
        }
      }

      return buffer;
    }
  };

  /**
   * InfiniteAmbientEngine Class
   */
  function InfiniteAmbientEngine(options) {
    options = options || {};
    this.basePath = options.basePath || getDefaultBasePath();
    this.audioContext = options.audioContext || null;
    this.masterVolume = options.masterVolume !== undefined ? options.masterVolume : 1.0;
    this.noiseType = options.noiseType || 'velvet';

    this.isInitialized = false;
    this.thunderCallbacks = [];
    this.thunderSchedulerTimer = null;
    this.channels = {};
    this.sampleBuffers = {};
    this.sampleLoadingPromises = {};

    // Prepare channel state structures
    var self = this;
    Object.keys(CHANNEL_DEFS).forEach(function (id) {
      var def = CHANNEL_DEFS[id];
      self.channels[id] = {
        id: id,
        name: def.name,
        category: def.category,
        file: def.file,
        weatherCategory: def.weatherCategory,
        level: 0.0,
        tone: def.defaultTone,
        spaceSend: def.defaultSend,
        isPlaying: false,
        sampleLoaded: false,
        sampleLoading: false,
        // WebAudio nodes
        masterGain: null,
        toneFilter: null,
        spaceSendGain: null,
        procGain: null,
        procSource: null,
        sampGain: null,
        sampSource: null,
        panner: null
      };
    });

    if (options.autoInit && typeof window !== 'undefined') {
      this.init();
    }
  }

  /**
   * Initialize WebAudio graph, master bus, and space reverb/delay network
   */
  InfiniteAmbientEngine.prototype.init = function () {
    if (this.isInitialized) return this;

    var AudioContextClass = typeof window !== 'undefined' ? (window.AudioContext || window.webkitAudioContext) : (typeof globalThis !== 'undefined' ? (globalThis.AudioContext || globalThis.webkitAudioContext) : null);
    if (!AudioContextClass) {
      if (typeof console !== 'undefined' && console.warn) {
        console.warn('[InfiniteAmbientEngine] WebAudio AudioContext not found (headless/SSR environment).');
      }
      return this;
    }

    if (!this.audioContext) {
      this.audioContext = new AudioContextClass();
    }

    var ctx = this.audioContext;

    // Master sum gain node
    this.masterGainNode = ctx.createGain();
    this.masterGainNode.gain.setValueAtTime(this.masterVolume, ctx.currentTime);
    this.masterGainNode.connect(ctx.destination);

    // Build Infinite Space Reverb / Delay Bus
    this._buildSpaceBus(ctx);

    // Initialize channel audio graph nodes
    var self = this;
    Object.keys(this.channels).forEach(function (id) {
      self._initChannelGraph(id);
    });

    // Start background thunder strike monitor
    this._startThunderMonitor();

    this.isInitialized = true;
    return this;
  };

  /**
   * Share AudioContext and external Space Bus with PianoEngine
   */
  InfiniteAmbientEngine.prototype.attachAudioContext = function (sharedCtx, sharedSpaceNode, sharedOutNode) {
    if (!sharedCtx) return this;
    if (!this.isInitialized) {
      this.audioContext = sharedCtx;
      this.init();
    }
    if (sharedOutNode && this.masterGainNode) {
      try {
        this.masterGainNode.disconnect();
        this.masterGainNode.connect(sharedOutNode);
      } catch (e) {}
    }
    if (sharedSpaceNode && this.spaceBusInput) {
      try {
        this.spaceBusInput.connect(sharedSpaceNode);
      } catch (e) {}
    }
    return this;
  };

  /**
   * Resume audio context on user interaction
   */
  InfiniteAmbientEngine.prototype.resume = function () {
    if (!this.isInitialized) {
      this.init();
    }
    if (this.audioContext && this.masterGainNode) {
      this.masterGainNode.gain.setTargetAtTime(this.masterVolume, this.audioContext.currentTime, 0.08);
    }
    if (this.audioContext && this.audioContext.state === 'suspended') {
      return this.audioContext.resume();
    }
    return Promise.resolve();
  };

  /**
   * Start / Resume playback alias
   */
  InfiniteAmbientEngine.prototype.start = function () {
    return this.resume();
  };

  /**
   * Pause ambient audio
   */
  InfiniteAmbientEngine.prototype.pause = function () {
    if (this.audioContext && this.masterGainNode) {
      this.masterGainNode.gain.setTargetAtTime(0.0, this.audioContext.currentTime, 0.1);
    }
    return this;
  };

  /**
   * Global Space reverb/delay depth (0.0 .. 1.0)
   */
  InfiniteAmbientEngine.prototype.setGlobalSpace = function (val) {
    var v = Math.max(0.0, Math.min(1.0, Number(val) || 0.0));
    this.globalSpace = v;
    if (this.spaceReturn && this.audioContext) {
      this.spaceReturn.gain.setTargetAtTime(v * 0.7, this.audioContext.currentTime, 0.05);
    }
    return this;
  };

  /**
   * Setup algorithmic space bus (reverb + ping-pong delay)
   */
  InfiniteAmbientEngine.prototype._buildSpaceBus = function (ctx) {
    this.spaceBusInput = ctx.createGain();
    this.spaceBusInput.gain.setValueAtTime(1.0, ctx.currentTime);

    // Space Output Return
    this.spaceReturn = ctx.createGain();
    if (this.spaceReturn.gain && this.spaceReturn.gain.setValueAtTime) {
      this.spaceReturn.gain.setValueAtTime(0.45, ctx.currentTime);
    }

    // Algorithmic Space Reverb (synthesized impulse response convolver)
    try {
      var revDur = 2.8;
      var revLen = Math.floor(ctx.sampleRate * revDur);
      var revBuf = ctx.createBuffer(2, revLen, ctx.sampleRate);
      var rl = revBuf.getChannelData(0);
      var rr = revBuf.getChannelData(1);
      for (var i = 0; i < revLen; i++) {
        var decay = Math.exp(-i / (ctx.sampleRate * 0.75));
        rl[i] = (Math.random() * 2 - 1) * decay;
        rr[i] = (Math.random() * 2 - 1) * decay;
      }
      this.spaceConvolver = ctx.createConvolver();
      this.spaceConvolver.buffer = revBuf;
      this.spaceBusInput.connect(this.spaceConvolver);
      this.spaceConvolver.connect(this.spaceReturn);
    } catch (e) {
      this.spaceConvolver = null;
    }

    // Ping-pong Tape Delay
    this.delayNode = ctx.createDelay ? ctx.createDelay(1.5) : null;
    if (this.delayNode) {
      if (this.delayNode.delayTime && this.delayNode.delayTime.setValueAtTime) {
        this.delayNode.delayTime.setValueAtTime(0.38, ctx.currentTime);
      }
      this.delayFeedback = ctx.createGain();
      if (this.delayFeedback.gain && this.delayFeedback.gain.setValueAtTime) {
        this.delayFeedback.gain.setValueAtTime(0.32, ctx.currentTime);
      }
      this.delayFilter = ctx.createBiquadFilter();
      this.delayFilter.type = 'lowpass';
      if (this.delayFilter.frequency && this.delayFilter.frequency.setValueAtTime) {
        this.delayFilter.frequency.setValueAtTime(2600, ctx.currentTime);
      }

      // Feedback loop
      this.delayNode.connect(this.delayFilter);
      this.delayFilter.connect(this.delayFeedback);
      this.delayFeedback.connect(this.delayNode);

      this.spaceBusInput.connect(this.delayNode);
      this.delayFilter.connect(this.spaceReturn);
    }

    // Connect space return to master
    this.spaceReturn.connect(this.masterGainNode);
  };

  /**
   * Construct node graph for a single channel
   */
  InfiniteAmbientEngine.prototype._initChannelGraph = function (id) {
    var ctx = this.audioContext;
    var ch = this.channels[id];

    // Channel master gain (controls channel volume)
    ch.masterGain = ctx.createGain();
    ch.masterGain.gain.setValueAtTime(ch.level, ctx.currentTime);

    // Tone filter (warm lowpass)
    ch.toneFilter = ctx.createBiquadFilter();
    ch.toneFilter.type = 'lowpass';
    ch.toneFilter.frequency.setValueAtTime(toneToFrequency(ch.tone), ctx.currentTime);
    ch.toneFilter.Q.setValueAtTime(0.7, ctx.currentTime);

    // Space send gain (sends signal to spaceBusInput)
    ch.spaceSendGain = ctx.createGain();
    ch.spaceSendGain.gain.setValueAtTime(ch.spaceSend, ctx.currentTime);

    // Procedural Layer Gain
    ch.procGain = ctx.createGain();
    ch.procGain.gain.setValueAtTime(0.0, ctx.currentTime);

    // Sampler Layer Gain
    ch.sampGain = ctx.createGain();
    ch.sampGain.gain.setValueAtTime(0.0, ctx.currentTime);

    // Stereo Panner (if available)
    if (ctx.createStereoPanner) {
      ch.panner = ctx.createStereoPanner();
      ch.panner.pan.setValueAtTime(0.0, ctx.currentTime);
      ch.toneFilter.connect(ch.panner);
      ch.panner.connect(ch.masterGain);
    } else {
      ch.toneFilter.connect(ch.masterGain);
    }

    // Connect master gain to main output and space send
    ch.masterGain.connect(this.masterGainNode);
    ch.masterGain.connect(ch.spaceSendGain);
    ch.spaceSendGain.connect(this.spaceBusInput);
  };

  /**
   * Start procedural layer immediately (0ms latency)
   */
  InfiniteAmbientEngine.prototype._startProceduralLayer = function (id) {
    var ctx = this.audioContext;
    var ch = this.channels[id];
    if (!ctx || !ch) return;

    if (ch.procSource) {
      try { ch.procSource.stop(); } catch (e) {}
      ch.procSource.disconnect();
      ch.procSource = null;
    }

    var buffer;
    if (id === 'noise') {
      buffer = ProceduralSynth.generateNoiseBuffer(ctx, this.noiseType, 2.0);
    } else {
      buffer = ProceduralSynth.generateChannelBuffer(ctx, id);
    }

    var src = ctx.createBufferSource();
    src.buffer = buffer;
    src.loop = true;
    src.connect(ch.procGain);
    ch.procGain.connect(ch.toneFilter);

    // Ramp procedural gain in fast (0.05s)
    var targetProcGain = ch.sampleLoaded ? 0.25 : 1.0;
    ch.procGain.gain.cancelScheduledValues(ctx.currentTime);
    ch.procGain.gain.setValueAtTime(0.01, ctx.currentTime);
    ch.procGain.gain.exponentialRampToValueAtTime(targetProcGain, ctx.currentTime + 0.05);

    src.start(0);
    ch.procSource = src;
  };

  /**
   * Fetch, decode, and start Infinite Sampler loop for a channel
   */
  InfiniteAmbientEngine.prototype._loadAndStartSample = function (id) {
    var self = this;
    var ctx = this.audioContext;
    var ch = this.channels[id];
    if (!ctx || !ch) return Promise.resolve();

    if (this.sampleBuffers[id]) {
      this._startSampleLooper(id, this.sampleBuffers[id]);
      return Promise.resolve(this.sampleBuffers[id]);
    }

    if (this.sampleLoadingPromises[id]) {
      return this.sampleLoadingPromises[id];
    }

    ch.sampleLoading = true;
    var url = this.basePath + ch.file;

    var p = fetch(url)
      .then(function (res) {
        if (!res.ok) throw new Error('HTTP ' + res.status + ' fetching ' + url);
        return res.arrayBuffer();
      })
      .then(function (ab) {
        return ctx.decodeAudioData(ab);
      })
      .then(function (audioBuffer) {
        self.sampleBuffers[id] = audioBuffer;
        ch.sampleLoaded = true;
        ch.sampleLoading = false;
        if (ch.level > 0.001) {
          self._startSampleLooper(id, audioBuffer);
        }
        return audioBuffer;
      })
      .catch(function (err) {
        console.warn('[InfiniteAmbientEngine] Could not load sample ' + url + ', using procedural layer:', err.message);
        ch.sampleLoading = false;
        // Keep procedural layer playing at 100% volume
        if (ch.procGain) {
          ch.procGain.gain.cancelScheduledValues(ctx.currentTime);
          ch.procGain.gain.setTargetAtTime(1.0, ctx.currentTime, 0.2);
        }
      });

    this.sampleLoadingPromises[id] = p;
    return p;
  };

  /**
   * Start seamless crossfade looper on loaded AudioBuffer
   */
  InfiniteAmbientEngine.prototype._startSampleLooper = function (id, audioBuffer) {
    var ctx = this.audioContext;
    var ch = this.channels[id];
    if (!ctx || !ch || !audioBuffer) return;

    if (ch.sampSource) {
      try { ch.sampSource.stop(); } catch (e) {}
      ch.sampSource.disconnect();
      ch.sampSource = null;
    }

    var src = ctx.createBufferSource();
    src.buffer = audioBuffer;
    src.loop = true;
    src.loopStart = 0;
    src.loopEnd = audioBuffer.duration;

    // Organic micro-detune drift (+/- 1.5 cents)
    src.detune.setValueAtTime((Math.random() * 3 - 1.5), ctx.currentTime);

    src.connect(ch.sampGain);
    ch.sampGain.connect(ch.toneFilter);

    // Crossfade sampler layer in smoothly over 0.3s
    ch.sampGain.gain.cancelScheduledValues(ctx.currentTime);
    ch.sampGain.gain.setValueAtTime(0.001, ctx.currentTime);
    ch.sampGain.gain.exponentialRampToValueAtTime(1.0, ctx.currentTime + 0.3);

    // Blend procedural layer down to 0.20 to provide organic micro-variation underneath
    if (ch.procGain) {
      ch.procGain.gain.cancelScheduledValues(ctx.currentTime);
      ch.procGain.gain.setTargetAtTime(0.20, ctx.currentTime, 0.4);
    }

    src.start(0);
    ch.sampSource = src;
    ch.isPlaying = true;
  };

  /**
   * Stop channel audio nodes
   */
  InfiniteAmbientEngine.prototype._stopChannelAudio = function (id) {
    var ctx = this.audioContext;
    var ch = this.channels[id];
    if (!ch) return;

    ch.isPlaying = false;
    var now = ctx ? ctx.currentTime : 0;

    if (ch.masterGain && ctx) {
      ch.masterGain.gain.cancelScheduledValues(now);
      ch.masterGain.gain.setTargetAtTime(0.0, now, 0.08);
    }

    setTimeout(function () {
      if (ch.level <= 0.001) {
        if (ch.procSource) {
          try { ch.procSource.stop(); } catch (e) {}
          ch.procSource.disconnect();
          ch.procSource = null;
        }
        if (ch.sampSource) {
          try { ch.sampSource.stop(); } catch (e) {}
          ch.sampSource.disconnect();
          ch.sampSource = null;
        }
      }
    }, 250);
  };

  /**
   * Public API: setChannelLevel(id, level)
   * @param {string} id - Channel identifier ('rain', 'vinyl', etc.)
   * @param {number} level - Volume level 0.0 to 1.0
   */
  InfiniteAmbientEngine.prototype.setChannelLevel = function (id, level) {
    var ch = this.channels[id];
    if (!ch) {
      console.warn('[InfiniteAmbientEngine] Unknown channel:', id);
      return this;
    }

    level = Math.max(0.0, Math.min(1.0, Number(level) || 0.0));
    ch.level = level;

    if (!this.isInitialized && level > 0) {
      this.init();
    }

    if (this.audioContext && ch.masterGain) {
      var ctx = this.audioContext;
      if (ctx.state === 'suspended' && level > 0) {
        ctx.resume();
      }

      var now = ctx.currentTime;
      ch.masterGain.gain.cancelScheduledValues(now);

      if (level > 0.001) {
        ch.isPlaying = true;
        // Smooth logarithmic volume curve
        var v = level * level;
        ch.masterGain.gain.setTargetAtTime(Math.max(0.001, v), now, 0.06);

        // Start procedural layer immediately (0ms)
        if (!ch.procSource) {
          this._startProceduralLayer(id);
        }
        // Fetch and engage sampler looper
        if (!ch.sampSource && !ch.sampleLoading) {
          this._loadAndStartSample(id);
        }
      } else {
        this._stopChannelAudio(id);
      }
    }

    return this;
  };

  /**
   * Public API: setChannelTone(id, tone)
   * @param {string} id - Channel identifier
   * @param {number} tone - Tone value 0.0 (warm/dark) to 1.0 (bright/open)
   */
  InfiniteAmbientEngine.prototype.setChannelTone = function (id, tone) {
    var ch = this.channels[id];
    if (!ch) return this;

    tone = Math.max(0.0, Math.min(1.0, Number(tone) || 0.0));
    ch.tone = tone;

    if (this.audioContext && ch.toneFilter) {
      var freq = toneToFrequency(tone);
      ch.toneFilter.frequency.setTargetAtTime(freq, this.audioContext.currentTime, 0.05);
    }
    return this;
  };

  /**
   * Public API: setChannelSpaceSend(id, send)
   * @param {string} id - Channel identifier
   * @param {number} send - Reverb/delay space send 0.0 to 1.0
   */
  InfiniteAmbientEngine.prototype.setChannelSpaceSend = function (id, send) {
    var ch = this.channels[id];
    if (!ch) return this;

    send = Math.max(0.0, Math.min(1.0, Number(send) || 0.0));
    ch.spaceSend = send;

    if (this.audioContext && ch.spaceSendGain) {
      ch.spaceSendGain.gain.setTargetAtTime(send, this.audioContext.currentTime, 0.05);
    }
    return this;
  };

  /**
   * Public API: setNoiseType(type)
   * @param {'pink'|'brown'|'white'|'velvet'} type
   */
  InfiniteAmbientEngine.prototype.setNoiseType = function (type) {
    var allowed = ['pink', 'brown', 'white', 'velvet'];
    if (allowed.indexOf(type) === -1) {
      console.warn('[InfiniteAmbientEngine] Invalid noise type: ' + type + '. Choose from: ' + allowed.join(', '));
      return this;
    }
    this.noiseType = type;

    // Refresh procedural buffer if noise channel is active
    var ch = this.channels.noise;
    if (ch && ch.isPlaying && this.audioContext) {
      this._startProceduralLayer('noise');
    }
    return this;
  };

  /**
   * Public API: applyPreset(presetId)
   * @param {string} presetId - Preset key ('midnight_grove', 'rainy_vinyl', etc.)
   */
  InfiniteAmbientEngine.prototype.applyPreset = function (presetId) {
    var preset = PRESETS[presetId];
    if (!preset) {
      console.warn('[InfiniteAmbientEngine] Unknown preset:', presetId);
      return this;
    }

    var self = this;
    var targetLevels = preset.levels || {};

    Object.keys(this.channels).forEach(function (id) {
      var targetLevel = targetLevels[id] !== undefined ? targetLevels[id] : 0.0;
      self.setChannelLevel(id, targetLevel);
    });

    return this;
  };

  /**
   * Public API: getActiveSummary()
   * Returns state telemetry for the 3D Minecraft Night Visualizer
   */
  InfiniteAmbientEngine.prototype.getActiveSummary = function () {
    var channelsSummary = {};
    var totalEnergy = 0.0;
    var activeCount = 0;

    var weatherState = {
      rain: 0.0,
      thunder: 0.0,
      fire: 0.0,
      wind: 0.0,
      crickets: 0.0,
      water: 0.0
    };

    var self = this;
    Object.keys(this.channels).forEach(function (id) {
      var ch = self.channels[id];
      channelsSummary[id] = {
        level: ch.level,
        tone: ch.tone,
        spaceSend: ch.spaceSend,
        isPlaying: ch.isPlaying,
        sampleLoaded: ch.sampleLoaded,
        sampleLoading: ch.sampleLoading
      };

      if (ch.level > 0.001) {
        totalEnergy += ch.level * ch.level;
        activeCount++;
      }

      // Map to weather telemetry
      if (id === 'rain') weatherState.rain = ch.level;
      if (id === 'thunder') weatherState.thunder = ch.level;
      if (id === 'fire') weatherState.fire = ch.level;
      if (id === 'leaves') weatherState.wind = ch.level;
      if (id === 'crickets') weatherState.crickets = ch.level;

      if (id === 'water_drops' || id === 'waterfall' || id === 'pebbles' || id === 'soothing') {
        if (ch.level > weatherState.water) {
          weatherState.water = ch.level;
        }
      }
    });

    var normalizedEnergy = activeCount > 0 ? Math.min(1.0, Math.sqrt(totalEnergy / Math.max(1, activeCount)) * 1.2) : 0.0;

    return {
      channels: channelsSummary,
      totalEnergy: Number(normalizedEnergy.toFixed(3)),
      weatherState: {
        rain: Number(weatherState.rain.toFixed(3)),
        thunder: Number(weatherState.thunder.toFixed(3)),
        fire: Number(weatherState.fire.toFixed(3)),
        wind: Number(weatherState.wind.toFixed(3)),
        crickets: Number(weatherState.crickets.toFixed(3)),
        water: Number(weatherState.water.toFixed(3))
      },
      noiseType: this.noiseType,
      masterVolume: this.masterVolume,
      isInitialized: this.isInitialized
    };
  };

  /**
   * Public API: onThunderStrike(callback)
   * Register listener for lightning flash triggers in the 3D visualizer
   * @param {function(number):void} callback - Receives intensity (0.0 .. 1.0)
   */
  InfiniteAmbientEngine.prototype.onThunderStrike = function (callback) {
    if (typeof callback === 'function') {
      this.thunderCallbacks.push(callback);
    }
    return this;
  };

  /**
   * Public API: triggerThunderStrike(intensity)
   * Manually trigger lightning strike event
   */
  InfiniteAmbientEngine.prototype.triggerThunderStrike = function (intensity) {
    intensity = intensity !== undefined ? intensity : (0.7 + Math.random() * 0.3);
    for (var i = 0; i < this.thunderCallbacks.length; i++) {
      try {
        this.thunderCallbacks[i](intensity);
      } catch (e) {
        console.error('[InfiniteAmbientEngine] Thunder callback error:', e);
      }
    }
  };

  /**
   * Internal scheduler for dynamic lightning flashes when thunder channel is active
   */
  InfiniteAmbientEngine.prototype._startThunderMonitor = function () {
    var self = this;
    if (this.thunderSchedulerTimer) return;

    function scheduleNext() {
      // Strike interval between 7s and 18s when thunder channel is active
      var delay = 7000 + Math.random() * 11000;
      self.thunderSchedulerTimer = setTimeout(function () {
        var thunderCh = self.channels.thunder;
        if (thunderCh && thunderCh.level > 0.05) {
          var intensity = Math.min(1.0, thunderCh.level * (0.8 + Math.random() * 0.4));
          self.triggerThunderStrike(intensity);
        }
        scheduleNext();
      }, delay);
      if (self.thunderSchedulerTimer && typeof self.thunderSchedulerTimer.unref === 'function') {
        self.thunderSchedulerTimer.unref();
      }
    }

    scheduleNext();
  };

  /**
   * Public API: Master volume control
   */
  InfiniteAmbientEngine.prototype.setMasterVolume = function (val) {
    val = Math.max(0.0, Math.min(1.0, Number(val) || 0.0));
    this.masterVolume = val;
    if (this.audioContext && this.masterGainNode) {
      this.masterGainNode.gain.setTargetAtTime(val, this.audioContext.currentTime, 0.05);
    }
    return this;
  };

  InfiniteAmbientEngine.prototype.getMasterVolume = function () {
    return this.masterVolume;
  };

  InfiniteAmbientEngine.prototype.getNoiseType = function () {
    return this.noiseType;
  };

  InfiniteAmbientEngine.prototype.getChannelLevel = function (id) {
    return this.channels[id] ? this.channels[id].level : 0.0;
  };

  InfiniteAmbientEngine.prototype.getChannelTone = function (id) {
    return this.channels[id] ? this.channels[id].tone : 0.5;
  };

  InfiniteAmbientEngine.prototype.getChannelSpaceSend = function (id) {
    return this.channels[id] ? this.channels[id].spaceSend : 0.0;
  };

  InfiniteAmbientEngine.prototype.getPresets = function () {
    return PRESETS;
  };

  InfiniteAmbientEngine.prototype.getChannelDefs = function () {
    return CHANNEL_DEFS;
  };

  InfiniteAmbientEngine.prototype.dispose = function () {
    if (this.thunderSchedulerTimer) {
      clearTimeout(this.thunderSchedulerTimer);
      this.thunderSchedulerTimer = null;
    }
    var self = this;
    Object.keys(this.channels).forEach(function (id) {
      self.setChannelLevel(id, 0);
      var ch = self.channels[id];
      if (ch.procSource) {
        try { ch.procSource.stop(); } catch (e) {}
        ch.procSource = null;
      }
      if (ch.sampSource) {
        try { ch.sampSource.stop(); } catch (e) {}
        ch.sampSource = null;
      }
    });
    if (this.audioContext && this.audioContext.state !== 'closed') {
      try { this.audioContext.close(); } catch (e) {}
    }
    this.isInitialized = false;
    return this;
  };

  // Provide factory singleton getter
  var defaultInstance = null;
  InfiniteAmbientEngine.getInstance = function (options) {
    if (!defaultInstance) {
      defaultInstance = new InfiniteAmbientEngine(options);
    }
    return defaultInstance;
  };

  if (typeof window !== 'undefined') {
    window.InfiniteAmbientEngine = InfiniteAmbientEngine;
  }

  return InfiniteAmbientEngine;
}));
