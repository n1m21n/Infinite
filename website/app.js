/**
 * Infinite — Web Application Logic
 * Cosmic Nebula Canvas, Light/Dark Mode, Playable Audio Tracks, and Node Explorer
 */

// ==========================================================================
// 1. Theme Manager (Dark Mode & Light Mode)
// ==========================================================================
function initTheme() {
  const themeToggleBtn = document.getElementById('theme-toggle-btn');
  const savedTheme = localStorage.getItem('infinite-theme');
  const systemPrefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;

  const initialTheme = savedTheme || (systemPrefersDark ? 'dark' : 'light');
  document.documentElement.setAttribute('data-theme', initialTheme);

  if (themeToggleBtn) {
    themeToggleBtn.addEventListener('click', () => {
      const current = document.documentElement.getAttribute('data-theme');
      const next = current === 'dark' ? 'light' : 'dark';
      document.documentElement.setAttribute('data-theme', next);
      localStorage.setItem('infinite-theme', next);
    });
  }
}

// ==========================================================================
// 2. Cosmic Nebula & Soft Star Dust Canvas (0.1 Opacity)
// ==========================================================================
const cosmicCanvas = document.getElementById('cosmic-canvas');
const cosmicCtx = cosmicCanvas ? cosmicCanvas.getContext('2d') : null;
let cWidth, cHeight;
let stars = [];
let nebulae = [];

function resizeCosmicCanvas() {
  if (!cosmicCanvas) return;
  cWidth = cosmicCanvas.width = window.innerWidth;
  cHeight = cosmicCanvas.height = window.innerHeight;
  initCosmos();
}

function initCosmos() {
  stars = [];
  nebulae = [];

  const starCount = Math.floor(Math.min(cWidth, 1400) / 18);
  for (let i = 0; i < starCount; i++) {
    stars.push({
      x: Math.random() * cWidth,
      y: Math.random() * cHeight,
      size: Math.random() * 1.5 + 0.5,
      alpha: Math.random() * 0.7 + 0.3,
      pulseSpeed: 0.005 + Math.random() * 0.015,
      pulseOffset: Math.random() * Math.PI * 2,
      vx: (Math.random() - 0.5) * 0.15,
      vy: (Math.random() - 0.5) * 0.15
    });
  }

  // Soft cosmic gas clusters
  for (let i = 0; i < 4; i++) {
    nebulae.push({
      x: Math.random() * cWidth,
      y: Math.random() * cHeight,
      radius: Math.random() * 200 + 250,
      vx: (Math.random() - 0.5) * 0.1,
      vy: (Math.random() - 0.5) * 0.1,
      color: ['#e07a5f', '#f4a261', '#81b29a', '#b8b8d1'][i % 4]
    });
  }
}

let frameCount = 0;
function animateCosmos() {
  if (!cosmicCtx) return;
  frameCount++;
  cosmicCtx.clearRect(0, 0, cWidth, cHeight);

  // Draw soft drifting nebula clusters
  nebulae.forEach(n => {
    n.x += n.vx;
    n.y += n.vy;
    if (n.x < -n.radius) n.x = cWidth + n.radius;
    if (n.x > cWidth + n.radius) n.x = -n.radius;
    if (n.y < -n.radius) n.y = cHeight + n.radius;
    if (n.y > cHeight + n.radius) n.y = -n.radius;

    const grad = cosmicCtx.createRadialGradient(n.x, n.y, 0, n.x, n.y, n.radius);
    grad.addColorStop(0, n.color);
    grad.addColorStop(1, 'transparent');

    cosmicCtx.fillStyle = grad;
    cosmicCtx.beginPath();
    cosmicCtx.arc(n.x, n.y, n.radius, 0, Math.PI * 2);
    cosmicCtx.fill();
  });

  // Draw twinkling star dust
  cosmicCtx.fillStyle = '#ffffff';
  stars.forEach(s => {
    s.x += s.vx;
    s.y += s.vy;
    if (s.x < 0) s.x = cWidth;
    if (s.x > cWidth) s.x = 0;
    if (s.y < 0) s.y = cHeight;
    if (s.y > cHeight) s.y = 0;

    const twinkle = Math.sin(frameCount * s.pulseSpeed + s.pulseOffset) * 0.3 + 0.7;
    cosmicCtx.globalAlpha = s.alpha * twinkle;
    cosmicCtx.beginPath();
    cosmicCtx.arc(s.x, s.y, s.size, 0, Math.PI * 2);
    cosmicCtx.fill();
  });
  cosmicCtx.globalAlpha = 1.0;

  requestAnimationFrame(animateCosmos);
}

// ==========================================================================
// 3. Showcase Tab Switcher
// ==========================================================================
function initShowcaseSwitcher() {
  const tabs = document.querySelectorAll('.showcase-tab');
  const items = document.querySelectorAll('.showcase-item');
  const titleEl = document.getElementById('showcase-title');

  const titles = {
    'media-daw': 'Infinite — Interactive Modular Canvas',
    'media-visual': 'Infinite — Live Generative Video & Shaders',
    'media-motion': 'Infinite — Procedural 3D & Soft Cloth Dynamics',
    'media-graph': 'Infinite — Node Graph Map (Pull-Based DAG)'
  };

  tabs.forEach(tab => {
    tab.addEventListener('click', () => {
      const targetId = tab.dataset.target;

      tabs.forEach(t => t.classList.remove('active'));
      items.forEach(item => item.classList.remove('active'));

      tab.classList.add('active');
      const targetPanel = document.getElementById(targetId);
      if (targetPanel) {
        targetPanel.classList.add('active');
        const video = targetPanel.querySelector('video');
        if (video) video.play();
      }

      if (titleEl && titles[targetId]) {
        titleEl.textContent = titles[targetId];
      }
    });
  });
}

// ==========================================================================
// 4. Playable Audio Track Showcase Player
// ==========================================================================
function initAudioPlayer() {
  const audio = document.getElementById('audio-element');
  const playBtn = document.getElementById('main-play-btn');
  const playIcon = document.getElementById('play-icon');
  const pauseIcon = document.getElementById('pause-icon');
  const progressContainer = document.getElementById('progress-container');
  const progressFill = document.getElementById('progress-fill');
  const timeCurrent = document.getElementById('time-current');
  const timeDuration = document.getElementById('time-duration');
  const trackBtns = document.querySelectorAll('.track-btn');
  const currentTitleEl = document.getElementById('current-track-title');

  if (!audio || !playBtn) return;

  function togglePlay() {
    if (audio.paused) {
      audio.play().then(() => {
        playIcon.style.display = 'none';
        pauseIcon.style.display = 'block';
      }).catch(() => {});
    } else {
      audio.pause();
      playIcon.style.display = 'block';
      pauseIcon.style.display = 'none';
    }
  }

  playBtn.addEventListener('click', togglePlay);

  audio.addEventListener('timeupdate', () => {
    if (audio.duration) {
      const progressPercent = (audio.currentTime / audio.duration) * 100;
      progressFill.style.width = `${progressPercent}%`;

      const curMins = Math.floor(audio.currentTime / 60);
      const curSecs = Math.floor(audio.currentTime % 60).toString().padStart(2, '0');
      timeCurrent.textContent = `${curMins}:${curSecs}`;

      const durMins = Math.floor(audio.duration / 60);
      const durSecs = Math.floor(audio.duration % 60).toString().padStart(2, '0');
      timeDuration.textContent = `${durMins}:${durSecs}`;
    }
  });

  audio.addEventListener('ended', () => {
    playIcon.style.display = 'block';
    pauseIcon.style.display = 'none';
    progressFill.style.width = '0%';
  });

  if (progressContainer) {
    progressContainer.addEventListener('click', (e) => {
      const width = progressContainer.clientWidth;
      const clickX = e.offsetX;
      if (audio.duration) {
        audio.currentTime = (clickX / width) * audio.duration;
      }
    });
  }

  // Playlist switching
  trackBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      trackBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');

      const src = btn.dataset.src;
      const title = btn.dataset.title;
      const duration = btn.dataset.duration;

      audio.src = src;
      if (currentTitleEl) currentTitleEl.textContent = title;
      if (timeDuration) timeDuration.textContent = duration;

      audio.play().then(() => {
        playIcon.style.display = 'none';
        pauseIcon.style.display = 'block';
      }).catch(() => {});
    });
  });
}

// ==========================================================================
// 5. Plain-Language 160+ Node Explorer Database
// ==========================================================================
const NODES_DATA = [
  // Sources
  { name: 'Video Player', category: 'source', catLabel: 'Source', desc: 'Play any video file with smooth looping, slow motion, and speed controls.' },
  { name: 'Image Loader', category: 'source', catLabel: 'Source', desc: 'Load photos, textures, illustrations, and HDR skies onto the canvas.' },
  { name: 'Syphon Receiver', category: 'source', catLabel: 'Source', desc: 'Stream live video straight from OBS, Resolume, or TouchDesigner with zero lag.' },
  { name: 'Formula Shader', category: 'source', catLabel: 'Source', desc: 'Write custom GLSL code or pick from 16 animated visual presets.' },
  { name: 'Paint Brush', category: 'source', catLabel: 'Source', desc: 'Hand-paint procedural brushes directly onto the screen with stroke recording.' },
  { name: 'Noise Generator', category: 'source', catLabel: 'Source', desc: 'Creates organic textures: clouds, marble, water ripples, and static.' },
  { name: '2D Shapes', category: 'source', catLabel: 'Source', desc: 'Generates clean vector-like circles, stars, polygons, hearts, and rings.' },
  { name: 'Text & Titles', category: 'source', catLabel: 'Source', desc: 'Render sharp typography using any font installed on your Mac.' },

  // 2D & Color
  { name: 'Remove Background', category: 'effects2d', catLabel: '2D & Color', desc: 'AI subject segmentation runs on your Apple Neural Engine with zero internet.' },
  { name: 'Blur & Soften', category: 'effects2d', catLabel: '2D & Color', desc: 'Add buttery smooth Gaussian, motion, and radial camera blurs.' },
  { name: 'Bloom & Glow', category: 'effects2d', catLabel: '2D & Color', desc: 'Adds dreamy cinematic light glows to the bright parts of your image.' },
  { name: 'Color Curves', category: 'effects2d', catLabel: '2D & Color', desc: 'Photoshop-style tone curves to balance contrast, shadow, and highlights.' },
  { name: 'Color Ramp', category: 'effects2d', catLabel: '2D & Color', desc: 'Map multi-color gradients across lighting and image brightness.' },
  { name: 'Blend Modes', category: 'effects2d', catLabel: '2D & Color', desc: '31 Photoshop blending modes (Multiply, Screen, Overlay, Soft Light).' },
  { name: 'Glitch & VHS', category: 'effects2d', catLabel: '2D & Color', desc: '6 retro effects: RGB split, analog VHS noise, and digital scanline jitter.' },
  { name: 'Kaleidoscope', category: 'effects2d', catLabel: '2D & Color', desc: 'Turn any video or image into hypnotic repeating mirror mandalas.' },
  { name: '3D LUT Colorist', category: 'effects2d', catLabel: '2D & Color', desc: 'Apply cinematic film grading LUT (.cube) files directly to your visuals.' },

  // 3D & Physics
  { name: '3D Shapes', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Create spheres, cubes, donuts (torus), cylinders, and terrain planes.' },
  { name: '3D Model Import', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Drag and drop 3D files (OBJ, PLY, STL, USDZ) directly onto the canvas.' },
  { name: 'Cloth Simulation', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Simulate soft blowing fabrics and flags with realistic digital wind.' },
  { name: 'Ocean Waves', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Generate a rolling 3D ocean surface with adjustable choppiness and foam.' },
  { name: 'Particle Clouds', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Emit thousands of swirling dust particles that react to gravity and force fields.' },
  { name: 'Instance on Points', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Scatter tens of thousands of objects across surfaces in a single draw call.' },
  { name: 'Realistic Lighting', category: 'geometry3d', catLabel: '3D & Physics', desc: 'Photorealistic Cook-Torrance GGX shaders with 32-bit HDRI reflection maps.' },

  // Synths & Audio
  { name: 'Wavetable Synth', category: 'synths', catLabel: 'Synths & Audio', desc: '12 multi-frame wavetables with rich unison detuning and stereo filters.' },
  { name: 'Modal Resonator', category: 'synths', catLabel: 'Synths & Audio', desc: 'Acoustic physical modeling for bells, chimes, glass cups, and metal plates.' },
  { name: 'PaulStretch Engine', category: 'synths', catLabel: 'Synths & Audio', desc: 'Time-stretches any audio clip up to 50x into endless ambient soundscapes.' },
  { name: 'Granular Player', category: 'synths', catLabel: 'Synths & Audio', desc: 'Chops audio into tiny clouds of grains for lush, textured musical clouds.' },
  { name: 'Drum Sequencer', category: 'synths', catLabel: 'Synths & Audio', desc: '8-track step sequencer with lane mutes, swing, and sample triggers.' },
  { name: 'AU / VST3 Host', category: 'synths', catLabel: 'Synths & Audio', desc: 'Host your favorite commercial instrument and effect plugins inside the graph.' },
  { name: 'Parametric EQ', category: 'synths', catLabel: 'Synths & Audio', desc: 'Multi-band audio equalizer with interactive frequency curve display.' },
  { name: 'Reverb & Delay', category: 'synths', catLabel: 'Synths & Audio', desc: 'Rich diffusion space reverb and tempo-synced ping-pong stereo echo.' },

  // Notes & MIDI
  { name: 'MIDI Keyboard', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Plug in any USB or Bluetooth MIDI controller keyboard to play notes live.' },
  { name: 'Arpeggiator', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Turns single chords into rhythmic ascending, descending, or random melodies.' },
  { name: 'Bouncing Balls', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Physics simulation where balls bounce under gravity to trigger musical notes.' },
  { name: 'Chord Generator', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Press one key to generate rich musical chords (Maj7, Min9, Sus4).' },
  { name: 'Scale Quantizer', category: 'midi', catLabel: 'Notes & MIDI', desc: 'Snaps any random note to your chosen musical scale so you never play out of key.' },

  // Modulation & CV
  { name: 'Audio Analyzer', category: 'modulation', catLabel: 'Modulation', desc: 'Splits live sound into 8 frequency bands to modulate your visuals.' },
  { name: 'Video Analyzer', category: 'modulation', catLabel: 'Modulation', desc: 'Extracts brightness, colors, and motion from video to control sound synths.' },
  { name: 'LFO Oscillator', category: 'modulation', catLabel: 'Modulation', desc: 'Slow cyclic waves (sine, saw, random) that automatically turn knobs for you.' },
  { name: 'XY Motion Pad', category: 'modulation', catLabel: 'Modulation', desc: 'Record your mouse gestures on a 2D pad to loop expressive modulation paths.' }
];

function initNodeDirectory() {
  const nodesGrid = document.getElementById('nodes-grid');
  const searchInput = document.getElementById('node-search-input');
  const filterBtns = document.querySelectorAll('.node-filter-btn');

  let currentFilter = 'all';
  let searchQuery = '';

  function render() {
    if (!nodesGrid) return;

    const filtered = NODES_DATA.filter(node => {
      const matchCat = (currentFilter === 'all') || (node.category === currentFilter);
      const matchSearch = !searchQuery ||
        node.name.toLowerCase().includes(searchQuery) ||
        node.desc.toLowerCase().includes(searchQuery) ||
        node.catLabel.toLowerCase().includes(searchQuery);
      return matchCat && matchSearch;
    });

    if (filtered.length === 0) {
      nodesGrid.innerHTML = `
        <div style="grid-column: 1 / -1; text-align: center; padding: 40px; color: var(--text-muted);">
          <p>No nodes found for "<strong>${escapeHtml(searchQuery)}</strong>"</p>
        </div>
      `;
      return;
    }

    nodesGrid.innerHTML = filtered.map(n => `
      <div class="node-pill-card">
        <div class="node-pill-header">
          <span class="node-pill-name">${escapeHtml(n.name)}</span>
          <span class="node-pill-cat">${escapeHtml(n.catLabel)}</span>
        </div>
        <p class="node-pill-desc">${escapeHtml(n.desc)}</p>
      </div>
    `).join('');
  }

  filterBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      filterBtns.forEach(b => b.classList.remove('active'));
      btn.classList.add('active');
      currentFilter = btn.dataset.filter;
      render();
    });
  });

  if (searchInput) {
    searchInput.addEventListener('input', (e) => {
      searchQuery = e.target.value.trim().toLowerCase();
      render();
    });
  }

  render();
}

function escapeHtml(str) {
  return str.replace(/[&<>"']/g, m => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[m]);
}

// ==========================================================================
// 6. Copy-to-Clipboard Helpers
// ==========================================================================
function initCopyButtons() {
  document.querySelectorAll('.copy-inline-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const text = btn.dataset.code;
      if (!text) return;
      navigator.clipboard.writeText(text).then(() => {
        const original = btn.innerText;
        btn.innerText = 'Copied!';
        btn.style.color = 'var(--pastel-peach)';
        setTimeout(() => {
          btn.innerText = original;
          btn.style.color = '';
        }, 2000);
      });
    });
  });
}

// DOM Ready
document.addEventListener('DOMContentLoaded', () => {
  initTheme();
  initShowcaseSwitcher();
  initAudioPlayer();
  initNodeDirectory();
  initCopyButtons();

  window.addEventListener('resize', resizeCosmicCanvas);
  resizeCosmicCanvas();
  requestAnimationFrame(animateCosmos);
});
