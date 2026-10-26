// Generative Plotter Contour Flow - Infinite Library (Sketch). Paste into a Sketch node's code editor.
// Pen plotter topographic contour isolines with undulating vector flow fields drawn on archival cream stock.
param("line_density", 42.0, 16.0, 80.0);
param("flow_scale", 3.2, 0.5, 8.0);
param("stream_warp", 0.65, 0.1, 1.5);
param("pen_weight", 1.8, 0.5, 4.0);
param("slate_density", 0.28, 0.0, 0.8);

function draw(t) {
  // Warm cream archival stock #F4EEDD
  background(0.957, 0.933, 0.867);

  const cx = width * 0.5;
  const cy = height * 0.5;
  const numLines = floor(line_density);
  const time = t * 0.4;

  noFill();

  // Corner plotter registration marks
  stroke(0.549, 0.114, 0.251, 0.7);
  strokeWeight(1.2);
  const pad = 40.0;
  const rlen = 20.0;
  line(pad, pad, pad + rlen, pad);
  line(pad, pad, pad, pad + rlen);
  line(width - pad, pad, width - pad - rlen, pad);
  line(width - pad, pad, width - pad, pad + rlen);
  line(pad, height - pad, pad + rlen, height - pad);
  line(pad, height - pad, pad, height - pad - rlen);
  line(width - pad, height - pad, width - pad - rlen, height - pad);
  line(width - pad, height - pad, width - pad, height - pad - rlen);

  // Topographic contour flow field
  const ptsPerLine = 140;
  const stepX = (width - pad * 2.0) / ptsPerLine;
  const stepY = (height - pad * 2.0) / (numLines + 1);

  for (let j = 0; j < numLines; j++) {
    const baseY = pad + (j + 1) * stepY;
    const normY = j / numLines;

    const isVermilion = (j % 11 === 3);
    const isSlate = (j % 5 === 1) && (slate_density > 0.1);

    if (isVermilion) {
      stroke(0.549, 0.114, 0.251, 0.92);
      strokeWeight(pen_weight * 1.3);
    } else if (isSlate) {
      stroke(0.157, 0.325, 0.420, 0.85);
      strokeWeight(pen_weight * 1.1);
    } else {
      stroke(0.129, 0.129, 0.129, 0.88);
      strokeWeight(pen_weight);
    }

    beginShape();
    for (let i = 0; i <= ptsPerLine; i++) {
      const x = pad + i * stepX;
      const u = (x - pad) / (width - pad * 2.0);

      const d1 = sin(u * flow_scale * 6.2831853 + time + normY * 3.5);
      const d2 = cos(u * flow_scale * 3.1415926 - time * 0.7 + normY * 5.0) * 0.5;
      const d3 = sin((u + normY) * flow_scale * 9.4247779 + time * 1.2) * 0.25;

      const envelope = sin(u * 3.14159265);
      const dispY = (d1 + d2 + d3) * (stepY * 2.2 * stream_warp) * envelope;

      vertex(x, baseY + dispY);
    }
    endShape();

    if (j % 7 === 0) {
      stroke(0.129, 0.129, 0.129, 0.45);
      strokeWeight(0.8);
      for (let i = 4; i < ptsPerLine; i += 6) {
        const x = pad + i * stepX;
        const u = (x - pad) / (width - pad * 2.0);
        const d1 = sin(u * flow_scale * 6.2831853 + time + normY * 3.5);
        const envelope = sin(u * 3.14159265);
        const y = baseY + d1 * (stepY * 2.2 * stream_warp) * envelope;
        line(x - 2.0, y - 5.0, x + 2.0, y + 5.0);
      }
    }
  }

  stroke(0.549, 0.114, 0.251, 0.8);
  strokeWeight(pen_weight * 1.1);
  ellipse(cx, cy, 180.0, 180.0);
}
