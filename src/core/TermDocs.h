#pragma once

// Language references for `infinite code docs`, for the languages AISkillContent.h does not cover.
// Every function named here is registered in src/core/sketch/SketchEngine.cpp or Sketch3DEngine.cpp,
// or defined in FormulaNode.cpp's preamble; tools/terminal/check_term_docs.py keeps it that way.
namespace TermDocs
{
   inline const char* kSketch = R"DOC(# Sketch: JavaScript 2D

Node: Sketch (Source). Sandboxed quickjs: no files, network or imports, 64 MB, 50 ms per draw.

## Shape of a script
    param("size", 48, 4, 160);        // top level: name, default, lo, hi. Becomes a knob and a global
    function draw(t) {                // called each frame, t = seconds
      background(0.05);
      fill(1, 0.4, 0.3);
      circle(width / 2, height / 2, size);
    }
Top-level code runs once, on compile. `draw` is required.

## Globals in draw
    width height   canvas size in pixels (the node's width and height)
    t beat frame   seconds, beats, frame count
    svgWidth svgHeight   size of the loaded SVG, 0 if none
    TAU PI HALF_PI CLOSE   Math is the normal JS Math

## Colour
Colours are 0..1 in p5's argument forms: (g) (g,a) (r,g,b) (r,g,b,a) or ([r,g,b,a]).
    background(...)  fill(...)  stroke(...)  noFill()  noStroke()  strokeWeight(w)
    hsl(h, s, l)     returns [r,g,b] for any colour call, h in 0..1

## Transform
    push()  pop()  translate(x, y)  rotate(radians)  scale(x, y)

## Shapes
    rect(x, y, w, h)  circle(x, y, diameter)  ellipse(x, y, w, h)  line(x1, y1, x2, y2)  point(x, y)
    triangle(x1,y1,x2,y2,x3,y3)  quad(x1,y1,...,x4,y4)  bezier(x1,y1,cx1,cy1,cx2,cy2,x2,y2)
    beginShape()  vertex(x, y)  endShape(CLOSE)

## Text
    textSize(px)  text(string, x, y)  textWidth(string)
    textFont("Menlo")  textAlign(LEFT | CENTER | RIGHT)

## Numbers
    random(lo, hi)   seeded per node and frame, so renders repeat
    noise(x, y, z)   smooth noise
    fract(x)         x - floor(x)

## SVG (drop an .svg on the node, or `s svg` in the patch)
    svgSet("#id", "attribute", value)  svgText("#id", string)  svgBox("#id") -> [x, y, w, h] or null
    svgDraw(x, y, w, h)   no arguments fills the canvas

## Wrong and right
    createCanvas(400, 400)          the canvas is the node's width and height; remove the call
    function setup() {}             never called; put setup at the top level
    colorMode(HSB)                  use hsl(h, s, l)
    fill(255, 0, 0)                 colours are 0..1: fill(1, 0, 0)
    circle(x, y, radius)            the third argument is the diameter
    let x = param("x", 0, 0, 1)     param() declares a global named x; just use x
)DOC";

   inline const char* kSketch3D = R"DOC(# Sketch 3D: JavaScript geometry

Node: Sketch 3D (3D). Same sandbox as Sketch. draw(t) builds a triangle mesh; the node's output is
geometry, so it needs a Render 3D to become a picture.

## Shape of a script
    param("count", 12, 1, 48);
    function draw(t) {
      for (let i = 0; i < count; i++) {
        push();
        rotateY(TAU * i / count + t);
        translate(1.2, 0, 0);
        fill(hsl(i / count, 0.7, 0.6));
        box(0.25);
        pop();
      }
    }

## Globals in draw
    t beat frame   seconds, beats, frame count
    TAU PI HALF_PI

## Colour
    fill(r, g, b)  in 0..1, sets vertex colour; a script that never calls fill has none
    hsl(h, s, l)   returns [r,g,b]

## Transform
    push()  pop()  translate(x, y, z)  rotate(angle, x, y, z)  rotateX(a)  rotateY(a)  rotateZ(a)
    scale(x, y, z)

## Solids (units are world units, not pixels; a shape near 1 fills the default camera)
    box(w, h, d)  plane(w, h)  sphere(r, detail)  cylinder(r, h, detail)  cone(r, h, detail)
    torus(radius, thickness, detail)
    tube(x1, y1, z1, x2, y2, z2, radius, sides)   a capless cylinder between two points

## Custom mesh
    beginShape(mode)  vertex(x, y, z, u, v)  endShape()

## Material attribute
    mask(v)   tags the vertices that follow; a material Field program reads it as `attr`

## Numbers
    random(lo, hi)  noise(x, y, z)  param(name, default, lo, hi)

## Limits
A frame that builds too many vertices or indices stops at the cap and reports it.

## Wrong and right
    background(0)            there is no canvas; set the background on Render 3D
    circle(0, 0, 1)          2D call; use plane or a flat cylinder
    box(100)                 world units: box(1)
    no Render 3D after it    a Sketch 3D alone shows nothing
)DOC";

   inline const char* kGlsl = R"DOC(# GLSL: the Shadertoy node

Node: Shadertoy (Source). Code from shadertoy.com pastes in as is. A file checked or pushed with
`infinite code` is the Image pass; `--pass bufa|bufb|bufc|bufd|common` picks another.

## Shape of a shader
    void mainImage(out vec4 fragColor, in vec2 fragCoord) {
      vec2 uv = fragCoord / iResolution.xy;
      fragColor = vec4(uv, 0.5 + 0.5 * sin(iTime), 1.0);
    }

## Provided by the preamble
    iTime iTimeDelta iFrame iResolution iMouse iDate   as on Shadertoy
    iChannel0 iChannel1 iChannel2 iChannel3            four image pins

## Passes
The node's `buffers` control sets how many of Buffer A to D run, 16-bit float. An unwired iChannelN
reads the buffer of the same number, so a buffer reading its own channel gets its previous frame.
Common is prepended to every pass.

## Knobs
    uniform float glow; // 0..2 = 0.5
A line in that form becomes a knob, up to 8, in source order, and can be modulated.

## Wrong and right
    void main()              write mainImage; the preamble calls it
    gl_FragCoord             use fragCoord
    uniform float iTime;     already defined, remove it
    texture2D(...)           use texture(...)
    error line numbers       count from the top of the pass, not of the compiled source

## Formula mode
The Formula node takes one GLSL expression or short body; `code new formula` starts one.
)DOC";

   inline const char* kView2D = R"DOC(# 2D View: the agent surface
Drive the open 2D document with `infinite view2d` (or the chat tool `view2d`). Reads change nothing; writes go through
a gate that checks every call first, then land as ONE undo step labelled "Agent: <label>". A batch that fails a check
changes nothing and answers typed errors: {code, call, message, near}. `near` lists the closest valid names.

## Reads
  infinite view2d layers                  every layer: id, kind, name, parent, fields
  infinite view2d fields                  field names, blends, presets, tips, tools, effects, styles, gradients, limits
  infinite view2d look [layer] [max]      a PNG of the picture (or one layer), at most `max` px a side (default 768),
                                          plus diff.changed [x0, y0, x1, y1]: what moved since the last same look
  {"op":"look","rect":[x0,y0,x1,y1],"inline":true}   a region; inline answers base64 instead of a file path

## Writes: infinite view2d '[{call}, {call}, ...]'   (or '-' to read the JSON from stdin)
Document px, 0..1 colours [r, g, b] or [r, g, b, a]. Layer -1 = the batch's last add.
  add      {"op":"add","kind":"paint|fill|text|group|adjust|node","above":id,"text":...,"type":"Curves"}
  set      {"op":"set","layer":id,"values":{"Opacity":0.5,"name":"sky","blend":"Multiply","color":[1,0,0]}}
  move     {"op":"move","layer":id,"target":id,"into":false}
  remove   {"op":"remove","layer":id}
  brush    {"op":"brush","preset":"Hard round","size":24,"hardness":0.8,"opacity":1,"flow":1,"spacing":0.1,
            "tool":"brush|eraser|blur|smudge|clone|heal|dodge|burn|...","tip":"...","pressureSize":true,"color":[..]}
           the agent's brush, kept between batches
  stroke   {"op":"stroke","layer":id,"points":[x0,y0,x1,y1,...],"pressure":[p0,p1,...],"tilt":[tx0,ty0,...],
            "target":"layer|mask|filterMask","sourceOffset":[dx,dy], any brush key to override}
  fill     {"op":"fill","layer":id,"at":[x,y],"color":[..],"tolerance":32,"contiguous":true,"sampleAll":false}
  gradient {"op":"gradient","layer":id,"from":[x,y],"to":[x,y],"colors":[[0,0,0],[1,1,1]],
            "kind":"linear|radial|angle|reflected|diamond","extend":"hold|repeat|reflect","opacity":1}
  path     {"op":"path","d":"M0 0 L100 0 L50 80 Z","fill":[1,0.8,0],"stroke":false,"width":4,"name":"tri"}
           or "points":[x0,y0,...] or "anchors":[x,y,inX,inY,outX,outY,...] with "closed"
  svg      {"op":"svg","svg":"<svg ...>...</svg>","at":[x,y],"scale":1}   editable vector layers, grouped
  select   {"op":"select","shape":S,"mode":"new|add|subtract|intersect", ...}
           S: rect / ellipse ("rect"), all, none, invert, expand / contract / smooth / border / feather ("px"),
           object ("rect" or "at"), subject, color_range ("samples":[[x,y]] or "colors":[[r,g,b]], "fuzziness"),
           grow / similar ("tolerance"), depth ("near":[lo,hi], 1 nearest; may answer "selection":"analysing")
  mask     {"op":"mask","layer":id,"action":"add|fromSelection|invert|apply|remove"}
  effect   {"op":"effect","layer":id,"action":"add|set|remove","name":"Curves","index":0,"amount":..,
            "points":[x0,y0,...],"levels":[inB,inW,outB,outW],"params":[..],"opacity":1,"blend":"Normal","on":true}
           `set` with no index on an adjustment layer edits the layer's own settings
  style    {"op":"style","layer":id,"name":"Drop Shadow","on":true,"color":[..],"angle":120,"distance":10,
            "size":10,"spread":0,"opacity":0.75,"position":"outside|inside|centre"}

## Undo
  infinite view2d undo | redo             only the agent's own steps; a user's edit on top answers E_UNDO

## Limits (E_LIMIT)
256 calls, 40000 stroke points and 64 new layers a batch; svg / path data 1 MB; look 32..2048 px.

## Loop
look -> decide -> one batch -> look again (diff.changed says where it landed) -> undo if it is wrong.
)DOC";
}
