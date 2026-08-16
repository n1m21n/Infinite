/**
 * Thermal Hand-Tracking Synth & Heatmap Engine
 * Manages MediaPipe Hands, 8 Thermal Heatmap Color Palettes, Web Audio, and Clean Minimal Right Panel
 */

document.addEventListener('DOMContentLoaded', async () => {
    const palettes = ['ironbow', 'magma', 'cyberpunk', 'turbo', 'inferno', 'viridis', 'acid', 'ice_fire'];
    let currentPaletteIndex = 0;

    const state = {
        isCameraRunning: false,
        isAudioRunning: false,
        mirrorCamera: true,
        showSkeleton: true,
        activePolygon: null,
        handLandmarks: [],
        lastFrameTime: performance.now(),
        fps: 60,
        simulatorActive: false,
        simMouse: { x: 500, y: 360, width: 340, height: 220 }
    };

    // DOM Elements
    const video = document.getElementById('webcam-video');
    const mainCanvas = document.getElementById('main-canvas');
    const mainCtx = mainCanvas.getContext('2d');

    const statusDot = document.getElementById('status-dot');
    const statusText = document.getElementById('status-text');
    const paletteItems = document.querySelectorAll('.palette-item');

    const btnCamera = document.getElementById('btn-camera');
    const btnAudio = document.getElementById('btn-audio');
    const btnFlip = document.getElementById('btn-flip');
    const btnSkeleton = document.getElementById('btn-skeleton');

    // Engines
    const audioEngine = new SynthAudioEngine();
    const filterEngine = new VideoFilterEngine();
    filterEngine.setMirrorCamera(state.mirrorCamera);

    function selectPalette(name) {
        const idx = palettes.indexOf(name);
        if (idx !== -1) {
            currentPaletteIndex = idx;
            filterEngine.setPalette(name);

            paletteItems.forEach(item => {
                item.classList.toggle('active', item.dataset.palette === name);
            });
        }
    }

    function cycleNextPalette() {
        currentPaletteIndex = (currentPaletteIndex + 1) % palettes.length;
        selectPalette(palettes[currentPaletteIndex]);
    }

    // Smooth Tracking Filter
    const smoothedPoints = [];
    function smoothPolygonPoints(targetPts, lerpFactor = 0.38) {
        while (smoothedPoints.length < targetPts.length) {
            smoothedPoints.push({ ...targetPts[smoothedPoints.length] });
        }
        while (smoothedPoints.length > targetPts.length) {
            smoothedPoints.pop();
        }
        for (let i = 0; i < targetPts.length; i++) {
            smoothedPoints[i].x += (targetPts[i].x - smoothedPoints[i].x) * lerpFactor;
            smoothedPoints[i].y += (targetPts[i].y - smoothedPoints[i].y) * lerpFactor;
        }
        return smoothedPoints.map(p => ({ x: p.x, y: p.y }));
    }

    // MediaPipe Hands Setup
    let hands = null;
    let cameraInstance = null;

    function initMediaPipe() {
        if (typeof Hands === 'undefined') return;

        hands = new Hands({
            locateFile: (file) => `https://cdn.jsdelivr.net/npm/@mediapipe/hands/${file}`
        });

        hands.setOptions({
            maxNumHands: 2,
            modelComplexity: 1,
            minDetectionConfidence: 0.55,
            minTrackingConfidence: 0.55
        });

        hands.onResults(onHandResults);
    }

    function onHandResults(results) {
        const width = mainCanvas.width;
        const height = mainCanvas.height;

        state.handLandmarks = [];
        if (results.multiHandLandmarks && results.multiHandLandmarks.length > 0) {
            state.handLandmarks = results.multiHandLandmarks;
        }

        const polygon = computePolygonFromHands(state.handLandmarks, width, height);
        state.activePolygon = polygon;

        if (polygon && polygon.points.length >= 3) {
            statusDot.className = 'status-dot active';
            statusText.textContent = 'Tracking';

            const normX = polygon.center.x / width;
            const normY = polygon.center.y / height;
            const areaNorm = Math.min(1.0, polygon.area / (width * height * 0.45));
            
            audioEngine.updateHandModulation({
                active: true,
                normalizedX: normX,
                normalizedY: normY,
                areaRatio: areaNorm,
                pinchDist: polygon.pinchDist || 0.5
            });
        } else {
            statusDot.className = 'status-dot';
            statusText.textContent = state.isCameraRunning ? 'Searching' : 'Standby';
            audioEngine.updateHandModulation({ active: false });
        }
    }

    function computePolygonFromHands(handsList, width, height) {
        if (!handsList || handsList.length === 0) {
            if (state.simulatorActive) {
                return getSimulatorPolygon();
            }
            return null;
        }

        const getCoord = (p) => ({
            x: (state.mirrorCamera ? (1.0 - p.x) : p.x) * width,
            y: p.y * height
        });

        let rawPts = [];
        let pinchDist = 0.5;

        if (handsList.length >= 2) {
            // TWO HANDS DETECTED: 4-point box spanning between Left Hand (Thumb, Index) and Right Hand (Index, Thumb)
            const h1 = handsList[0];
            const h2 = handsList[1];

            const h1X = getCoord(h1[0]).x;
            const h2X = getCoord(h2[0]).x;
            const leftHand = (h1X < h2X) ? h1 : h2;
            const rightHand = (h1X < h2X) ? h2 : h1;

            const lThumb = getCoord(leftHand[4]);
            const lIndex = getCoord(leftHand[8]);
            const rIndex = getCoord(rightHand[8]);
            const rThumb = getCoord(rightHand[4]);

            rawPts = [lThumb, lIndex, rIndex, rThumb];

            const dx = (rIndex.x - lIndex.x);
            const dy = (rIndex.y - lIndex.y);
            pinchDist = Math.min(1, Math.sqrt(dx * dx + dy * dy) / (width * 0.7));

        } else if (handsList.length === 1) {
            // SINGLE HAND: Extrude oriented box from thumb and index
            const h = handsList[0];
            const thumb = getCoord(h[4]);
            const index = getCoord(h[8]);

            const dx = index.x - thumb.x;
            const dy = index.y - thumb.y;
            const len = Math.sqrt(dx * dx + dy * dy);
            const nx = -dy / (len || 1) * (len * 0.85);
            const ny = dx / (len || 1) * (len * 0.85);

            rawPts = [
                { x: thumb.x, y: thumb.y },
                { x: index.x, y: index.y },
                { x: index.x + nx, y: index.y + ny },
                { x: thumb.x + nx, y: thumb.y + ny }
            ];
            pinchDist = Math.min(1, len / (width * 0.4));
        }

        if (rawPts.length < 3) return null;

        const smoothed = smoothPolygonPoints(rawPts, 0.4);

        let sumX = 0, sumY = 0;
        smoothed.forEach(p => { sumX += p.x; sumY += p.y; });
        const center = { x: sumX / smoothed.length, y: sumY / smoothed.length };

        let area = 0;
        for (let i = 0; i < smoothed.length; i++) {
            const j = (i + 1) % smoothed.length;
            area += smoothed[i].x * smoothed[j].y;
            area -= smoothed[j].x * smoothed[i].y;
        }
        area = Math.abs(area) * 0.5;

        return { points: smoothed, center, area, pinchDist };
    }

    function getSimulatorPolygon() {
        const { x, y, width, height } = state.simMouse;
        const w2 = width / 2;
        const h2 = height / 2;
        const pts = [
            { x: x - w2, y: y - h2 },
            { x: x + w2, y: y - h2 * 0.7 },
            { x: x + w2 * 0.8, y: y + h2 },
            { x: x - w2 * 0.9, y: y + h2 * 0.8 }
        ];
        return {
            points: pts,
            center: { x, y },
            area: width * height,
            pinchDist: 0.6
        };
    }

    // Camera Lifecycle
    async function startCamera() {
        try {
            btnCamera.textContent = 'Starting...';
            initMediaPipe();

            const stream = await navigator.mediaDevices.getUserMedia({
                video: {
                    width: { ideal: 1920, min: 1280 },
                    height: { ideal: 1080, min: 720 },
                    facingMode: 'user'
                },
                audio: false
            });

            video.srcObject = stream;
            await video.play();

            mainCanvas.width = video.videoWidth || 1280;
            mainCanvas.height = video.videoHeight || 720;

            if (typeof Camera !== 'undefined' && hands) {
                cameraInstance = new Camera(video, {
                    onFrame: async () => {
                        if (hands && state.isCameraRunning) {
                            await hands.send({ image: video });
                        }
                    },
                    width: mainCanvas.width,
                    height: mainCanvas.height
                });
                cameraInstance.start();
            }

            state.isCameraRunning = true;
            btnCamera.textContent = 'Stop Camera';
            btnCamera.classList.add('active');
        } catch (err) {
            console.error('Webcam error:', err);
            enableSimulator();
            btnCamera.textContent = 'Simulator';
        }
    }

    function stopCamera() {
        if (video.srcObject) {
            video.srcObject.getTracks().forEach(t => t.stop());
            video.srcObject = null;
        }
        if (cameraInstance) {
            cameraInstance.stop();
            cameraInstance = null;
        }
        state.isCameraRunning = false;
        btnCamera.textContent = 'Start Camera';
        btnCamera.classList.remove('active');
    }

    function enableSimulator() {
        state.simulatorActive = true;
        mainCanvas.width = 1280;
        mainCanvas.height = 720;
    }

    // Render Loop (60 FPS)
    function renderLoop() {
        const w = mainCanvas.width;
        const h = mainCanvas.height;

        if (state.isCameraRunning && video.readyState >= 2) {
            filterEngine.renderFrame(mainCtx, video, state.activePolygon, state.handLandmarks, w, h);
        } else if (state.simulatorActive) {
            _drawSimulatorBackground(mainCtx, w, h);
            const simPoly = getSimulatorPolygon();
            state.activePolygon = simPoly;
            filterEngine.renderFrame(mainCtx, mainCanvas, simPoly, [], w, h);

            if (audioEngine.isRunning) {
                audioEngine.updateHandModulation({
                    active: true,
                    normalizedX: simPoly.center.x / w,
                    normalizedY: simPoly.center.y / h,
                    areaRatio: simPoly.area / (w * h * 0.4),
                    pinchDist: 0.5
                });
            }
        }

        requestAnimationFrame(renderLoop);
    }

    function _drawSimulatorBackground(ctx, w, h) {
        ctx.fillStyle = '#090b10';
        ctx.fillRect(0, 0, w, h);
    }

    // Event Listeners
    paletteItems.forEach(item => {
        item.addEventListener('click', () => {
            selectPalette(item.dataset.palette);
        });
    });

    btnCamera.addEventListener('click', async () => {
        if (!state.isCameraRunning) {
            await startCamera();
            if (!audioEngine.isRunning) {
                await audioEngine.init();
                btnAudio.classList.add('active');
            }
        } else {
            stopCamera();
        }
    });

    btnAudio.addEventListener('click', async () => {
        if (!audioEngine.isRunning) {
            await audioEngine.init();
            btnAudio.classList.add('active');
        } else {
            const newMute = !audioEngine.isMuted;
            audioEngine.setMute(newMute);
            btnAudio.classList.toggle('active', !newMute);
        }
    });

    btnFlip.addEventListener('click', () => {
        state.mirrorCamera = !state.mirrorCamera;
        filterEngine.setMirrorCamera(state.mirrorCamera);
        btnFlip.classList.toggle('active', state.mirrorCamera);
    });

    btnSkeleton.addEventListener('click', () => {
        state.showSkeleton = !state.showSkeleton;
        filterEngine.showSkeleton = state.showSkeleton;
        btnSkeleton.classList.toggle('active', state.showSkeleton);
    });

    // Keyboard Shortcuts
    window.addEventListener('keydown', (e) => {
        if (e.code === 'Space' || e.code === 'ArrowRight' || e.code === 'ArrowDown') {
            e.preventDefault();
            cycleNextPalette();
        } else if (e.code === 'ArrowLeft' || e.code === 'ArrowUp') {
            e.preventDefault();
            currentPaletteIndex = (currentPaletteIndex - 1 + palettes.length) % palettes.length;
            selectPalette(palettes[currentPaletteIndex]);
        } else if (e.key >= '1' && e.key <= '8') {
            const idx = parseInt(e.key, 10) - 1;
            if (idx >= 0 && idx < palettes.length) {
                selectPalette(palettes[idx]);
            }
        } else if (e.code === 'KeyM') {
            btnAudio.click();
        }
    });

    // Mouse / Touch Drag Simulation Support
    let isDragging = false;
    mainCanvas.addEventListener('mousedown', (e) => {
        isDragging = true;
        updateSimCoords(e);
    });
    window.addEventListener('mousemove', (e) => {
        if (isDragging) updateSimCoords(e);
    });
    window.addEventListener('mouseup', () => {
        isDragging = false;
    });

    function updateSimCoords(e) {
        const rect = mainCanvas.getBoundingClientRect();
        const scaleX = mainCanvas.width / rect.width;
        const scaleY = mainCanvas.height / rect.height;
        state.simMouse.x = (e.clientX - rect.left) * scaleX;
        state.simMouse.y = (e.clientY - rect.top) * scaleY;
        if (!state.isCameraRunning) {
            state.simulatorActive = true;
        }
    }

    // Auto-start
    requestAnimationFrame(renderLoop);
    setTimeout(initMediaPipe, 800);
});
