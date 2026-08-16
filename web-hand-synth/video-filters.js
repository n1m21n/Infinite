/**
 * Thermal Heatmap & Polygon Filter Engine
 * Features:
 * - Ultra-fast 256-color LUT-based Thermal Heatmaps
 * - 8 Curated Color Palettes (Ironbow/FLIR, Turbo, Magma/Lava, Cyberpunk Neon, Inferno, Viridis, Acid Emerald, Ice & Fire)
 * - Clean, distraction-free rendering (no cluttering text or badges)
 * - Sleek glowing polygon wireframe & skeleton overlay
 * - Horizontal camera flip (mirrored)
 */

class VideoFilterEngine {
    constructor() {
        this.currentPalette = 'ironbow'; // 'ironbow', 'magma', 'cyberpunk', 'turbo', 'inferno', 'viridis', 'acid', 'ice_fire'
        this.showSkeleton = true;
        this.showWireframe = true;
        this.mirrorCamera = true;

        // Offscreen source canvas to hold flipped video feed
        this.sourceCanvas = document.createElement('canvas');
        this.sourceCtx = this.sourceCanvas.getContext('2d', { willReadFrequently: true });

        // Fast pixel processing canvas
        this.tempCanvas = document.createElement('canvas');
        this.tempCtx = this.tempCanvas.getContext('2d', { willReadFrequently: true });

        // Precompute LUT tables for all palettes (256 * 3 bytes each)
        this.luts = {};
        this._initPaletteLuts();

        this.animPhase = 0;
    }

    _initPaletteLuts() {
        const paletteDefinitions = {
            ironbow: [
                { pos: 0.0, rgb: [10, 0, 35] },      // Deep violet
                { pos: 0.2, rgb: [75, 0, 130] },     // Purple
                { pos: 0.42, rgb: [205, 20, 45] },   // Crimson Red
                { pos: 0.68, rgb: [255, 135, 0] },   // Vivid Orange
                { pos: 0.88, rgb: [255, 235, 60] },  // Bright Yellow
                { pos: 1.0, rgb: [255, 255, 255] }   // White Hot
            ],
            magma: [
                { pos: 0.0, rgb: [0, 0, 4] },        // Black
                { pos: 0.25, rgb: [81, 18, 124] },   // Dark Violet
                { pos: 0.5, rgb: [182, 54, 121] },   // Magenta Red
                { pos: 0.75, rgb: [251, 136, 97] },  // Warm Orange
                { pos: 1.0, rgb: [254, 251, 188] }   // Pale Yellow
            ],
            cyberpunk: [
                { pos: 0.0, rgb: [6, 2, 24] },       // Midnight Blue
                { pos: 0.28, rgb: [120, 0, 180] },   // Neon Purple
                { pos: 0.55, rgb: [255, 0, 120] },   // Hot Magenta
                { pos: 0.82, rgb: [0, 245, 255] },   // Electric Cyan
                { pos: 1.0, rgb: [255, 255, 255] }   // Laser White
            ],
            turbo: [
                { pos: 0.0, rgb: [48, 18, 59] },     // Indigo
                { pos: 0.25, rgb: [70, 134, 251] },  // Sky Blue
                { pos: 0.5, rgb: [27, 229, 139] },   // Emerald Green
                { pos: 0.75, rgb: [251, 185, 56] },  // Amber Yellow
                { pos: 1.0, rgb: [122, 4, 3] }       // Deep Red
            ],
            inferno: [
                { pos: 0.0, rgb: [0, 0, 4] },        // Black
                { pos: 0.3, rgb: [87, 16, 110] },    // Deep Purple
                { pos: 0.6, rgb: [187, 55, 47] },    // Fiery Red
                { pos: 0.85, rgb: [243, 160, 44] },  // Solar Gold
                { pos: 1.0, rgb: [252, 255, 164] }   // Solar White
            ],
            viridis: [
                { pos: 0.0, rgb: [68, 1, 84] },      // Deep Purple
                { pos: 0.35, rgb: [49, 104, 142] },  // Teal Blue
                { pos: 0.7, rgb: [53, 183, 121] },   // Mint Green
                { pos: 1.0, rgb: [253, 231, 37] }    // Electric Yellow
            ],
            acid: [
                { pos: 0.0, rgb: [0, 8, 4] },        // Deep Matrix Green
                { pos: 0.35, rgb: [0, 90, 45] },     // Dark Jade
                { pos: 0.7, rgb: [0, 255, 115] },    // Neon Acid Lime
                { pos: 1.0, rgb: [220, 255, 230] }   // Glowing Mint
            ],
            ice_fire: [
                { pos: 0.0, rgb: [0, 20, 70] },      // Deep Cold Blue
                { pos: 0.3, rgb: [0, 200, 255] },    // Cyan Ice
                { pos: 0.5, rgb: [255, 255, 255] },  // White Neutral
                { pos: 0.75, rgb: [255, 120, 0] },   // Warm Orange
                { pos: 1.0, rgb: [200, 0, 20] }      // Fiery Red
            ]
        };

        for (const [name, stops] of Object.entries(paletteDefinitions)) {
            const lut = new Uint8Array(256 * 3);
            for (let i = 0; i < 256; i++) {
                const t = i / 255;
                // Find bounding color stops
                let s1 = stops[0], s2 = stops[stops.length - 1];
                for (let k = 0; k < stops.length - 1; k++) {
                    if (t >= stops[k].pos && t <= stops[k + 1].pos) {
                        s1 = stops[k];
                        s2 = stops[k + 1];
                        break;
                    }
                }
                const range = (s2.pos - s1.pos) || 1;
                const factor = Math.max(0, Math.min(1, (t - s1.pos) / range));

                lut[i * 3] = Math.round(s1.rgb[0] + (s2.rgb[0] - s1.rgb[0]) * factor);
                lut[i * 3 + 1] = Math.round(s1.rgb[1] + (s2.rgb[1] - s1.rgb[1]) * factor);
                lut[i * 3 + 2] = Math.round(s1.rgb[2] + (s2.rgb[2] - s1.rgb[2]) * factor);
            }
            this.luts[name] = lut;
        }
    }

    setPalette(name) {
        if (this.luts[name]) {
            this.currentPalette = name;
        }
    }

    setMirrorCamera(mirror) {
        this.mirrorCamera = mirror;
    }

    /**
     * Main Render Loop for Video & Filtered Polygon
     */
    renderFrame(mainCtx, videoElement, polygon, handLandmarksList, canvasWidth, canvasHeight) {
        this.animPhase += 0.04;

        if (this.sourceCanvas.width !== canvasWidth || this.sourceCanvas.height !== canvasHeight) {
            this.sourceCanvas.width = canvasWidth;
            this.sourceCanvas.height = canvasHeight;
        }

        if (this.tempCanvas.width !== canvasWidth || this.tempCanvas.height !== canvasHeight) {
            this.tempCanvas.width = canvasWidth;
            this.tempCanvas.height = canvasHeight;
        }

        // 1. Prepare Mirrored Video Feed
        this.sourceCtx.save();
        this.sourceCtx.clearRect(0, 0, canvasWidth, canvasHeight);
        if (this.mirrorCamera) {
            this.sourceCtx.translate(canvasWidth, 0);
            this.sourceCtx.scale(-1, 1);
        }
        this.sourceCtx.drawImage(videoElement, 0, 0, canvasWidth, canvasHeight);
        this.sourceCtx.restore();

        // 2. Draw Clean Background Video (no grid, no clutter)
        mainCtx.save();
        mainCtx.clearRect(0, 0, canvasWidth, canvasHeight);
        mainCtx.drawImage(this.sourceCanvas, 0, 0, canvasWidth, canvasHeight);
        mainCtx.restore();

        // 3. Render Thermal Heatmap inside the Polygon
        if (polygon && polygon.points && polygon.points.length >= 3) {
            this._renderThermalPolygon(mainCtx, this.sourceCanvas, polygon, canvasWidth, canvasHeight);
            
            if (this.showWireframe) {
                this._drawCleanPolygonBorder(mainCtx, polygon);
            }
        }

        // 4. Draw Clean Hand Skeletons
        if (this.showSkeleton && handLandmarksList && handLandmarksList.length > 0) {
            this._drawHandSkeletons(mainCtx, handLandmarksList, canvasWidth, canvasHeight);
        }
    }

    /**
     * Applies the selected 256-color Thermal Heatmap LUT inside the clipped polygon
     */
    _renderThermalPolygon(ctx, sourceImage, polygon, width, height) {
        ctx.save();
        
        // Clip to exact polygon
        ctx.beginPath();
        const pts = polygon.points;
        ctx.moveTo(pts[0].x, pts[0].y);
        for (let i = 1; i < pts.length; i++) {
            ctx.lineTo(pts[i].x, pts[i].y);
        }
        ctx.closePath();
        ctx.clip();

        // Compute bounding box for high-speed sub-region processing
        let minX = width, minY = height, maxX = 0, maxY = 0;
        for (const p of pts) {
            if (p.x < minX) minX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.x > maxX) maxX = p.x;
            if (p.y > maxY) maxY = p.y;
        }

        const pad = 6;
        minX = Math.max(0, Math.floor(minX - pad));
        minY = Math.max(0, Math.floor(minY - pad));
        maxX = Math.min(width, Math.ceil(maxX + pad));
        maxY = Math.min(height, Math.ceil(maxY + pad));
        const bbWidth = maxX - minX;
        const bbHeight = maxY - minY;

        if (bbWidth <= 0 || bbHeight <= 0) {
            ctx.restore();
            return;
        }

        // Draw sub-region into temp canvas
        this.tempCanvas.width = bbWidth;
        this.tempCanvas.height = bbHeight;
        this.tempCtx.drawImage(sourceImage, minX, minY, bbWidth, bbHeight, 0, 0, bbWidth, bbHeight);

        const imgData = this.tempCtx.getImageData(0, 0, bbWidth, bbHeight);
        const data = imgData.data;
        const lut = this.luts[this.currentPalette] || this.luts['ironbow'];

        // Apply LUT transformation (optimized integer arithmetic)
        for (let i = 0; i < data.length; i += 4) {
            // Perceptual luminance calculation (0..255)
            const lum = (data[i] * 77 + data[i + 1] * 150 + data[i + 2] * 29) >> 8;
            const lutIdx = lum * 3;
            data[i] = lut[lutIdx];
            data[i + 1] = lut[lutIdx + 1];
            data[i + 2] = lut[lutIdx + 2];
        }

        this.tempCtx.putImageData(imgData, 0, 0);
        ctx.drawImage(this.tempCanvas, minX, minY);

        ctx.restore();
    }

    /**
     * Clean, minimalist glowing polygon wireframe border (no text, no clutter)
     */
    _drawCleanPolygonBorder(ctx, polygon) {
        const pts = polygon.points;
        if (!pts || pts.length < 3) return;

        ctx.save();

        // 1. Glowing outer border
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 2.5;
        ctx.shadowColor = '#00ffcc';
        ctx.shadowBlur = 10;

        ctx.beginPath();
        ctx.moveTo(pts[0].x, pts[0].y);
        for (let i = 1; i < pts.length; i++) {
            ctx.lineTo(pts[i].x, pts[i].y);
        }
        ctx.closePath();
        ctx.stroke();

        // 2. Corner anchor dots
        pts.forEach(p => {
            ctx.fillStyle = '#ffffff';
            ctx.shadowColor = '#00ffff';
            ctx.shadowBlur = 12;
            ctx.beginPath();
            ctx.arc(p.x, p.y, 4.5, 0, Math.PI * 2);
            ctx.fill();
        });

        // 3. Subtle inner wireframe depth loop for 4-point boxes
        if (pts.length === 4) {
            const cX = (pts[0].x + pts[1].x + pts[2].x + pts[3].x) / 4;
            const cY = (pts[0].y + pts[1].y + pts[2].y + pts[3].y) / 4;

            ctx.strokeStyle = 'rgba(255, 255, 255, 0.35)';
            ctx.lineWidth = 1.2;
            ctx.beginPath();
            for (let i = 0; i < 4; i++) {
                const innerX = pts[i].x + (cX - pts[i].x) * 0.12;
                const innerY = pts[i].y + (cY - pts[i].y) * 0.12;
                if (i === 0) ctx.moveTo(innerX, innerY);
                else ctx.lineTo(innerX, innerY);

                ctx.moveTo(pts[i].x, pts[i].y);
                ctx.lineTo(innerX, innerY);
            }
            ctx.closePath();
            ctx.stroke();
        }

        ctx.restore();
    }

    /**
     * Draw Hand Skeleton (Bones in clean green, Fingertips in clean red)
     */
    _drawHandSkeletons(ctx, handLandmarksList, width, height) {
        const connections = [
            [0, 1], [1, 2], [2, 3], [3, 4],
            [0, 5], [5, 6], [6, 7], [7, 8],
            [0, 9], [9, 10], [10, 11], [11, 12],
            [0, 13], [13, 14], [14, 15], [15, 16],
            [0, 17], [17, 18], [18, 19], [19, 20],
            [5, 9], [9, 13], [13, 17]
        ];

        ctx.save();

        const getCoord = (p) => ({
            x: (this.mirrorCamera ? (1.0 - p.x) : p.x) * width,
            y: p.y * height
        });

        handLandmarksList.forEach(landmarks => {
            // Bones
            ctx.strokeStyle = '#00ff44';
            ctx.lineWidth = 3;
            ctx.shadowColor = '#00ff44';
            ctx.shadowBlur = 6;

            for (const [startIdx, endIdx] of connections) {
                const pStart = getCoord(landmarks[startIdx]);
                const pEnd = getCoord(landmarks[endIdx]);

                ctx.beginPath();
                ctx.moveTo(pStart.x, pStart.y);
                ctx.lineTo(pEnd.x, pEnd.y);
                ctx.stroke();
            }

            // Joints
            landmarks.forEach((pRaw, idx) => {
                const p = getCoord(pRaw);
                const isKeyFingertip = (idx === 4 || idx === 8);
                const radius = isKeyFingertip ? 6.5 : 4;

                ctx.fillStyle = '#ff2233';
                ctx.shadowColor = '#ff2233';
                ctx.shadowBlur = 8;
                ctx.beginPath();
                ctx.arc(p.x, p.y, radius, 0, Math.PI * 2);
                ctx.fill();

                if (isKeyFingertip) {
                    ctx.fillStyle = '#ffffff';
                    ctx.beginPath();
                    ctx.arc(p.x, p.y, 2.5, 0, Math.PI * 2);
                    ctx.fill();
                }
            });
        });

        ctx.restore();
    }
}

window.VideoFilterEngine = VideoFilterEngine;
