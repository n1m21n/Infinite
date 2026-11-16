const fs = require("fs");
const path = require("path");
const os = require("os");
const readline = require("readline");
const effects = require("./effects");
const { ATTRIBUTION, CREDIT_LINE, SERVER_INSTRUCTIONS, findBinary, escapeCode, runCommand } = require("./engine");

const TOOLS = [
  {
    name: "infinite_synthesize_field_synth",
    description: "synthesize wav audio from an inline field synth per-sample dsp kernel using infinite c++ engine",
    inputSchema: {
      type: "object",
      properties: {
        code: {
          type: "string",
          description: "field dsp code (e.g. 'state float phase = 0\\nphase = (phase + freq / sr) % 1.0\\nout = sin(6.28318 * phase) * 0.4 * gate')"
        },
        duration: {
          type: "number",
          description: "duration in seconds (default 3.0)"
        },
        out_wav: {
          type: "string",
          description: "optional output .wav path"
        }
      },
      required: ["code"]
    }
  },
  {
    name: "infinite_synthesize_field_notes",
    description: "generate algorithmic midi sequences and euclidean rhythms with field notes node into audio",
    inputSchema: {
      type: "object",
      properties: {
        code: {
          type: "string",
          description: "field notes logic (e.g. 'if (tick(1/8) > 0.5) { note(deg(floor(rand() * 7)), 0.8, 1/8) }')"
        },
        synth_type: {
          type: "string",
          enum: ["wavetable", "fm"],
          description: "synth engine to receive notes"
        },
        duration: {
          type: "number",
          description: "duration in seconds"
        },
        out_wav: {
          type: "string",
          description: "optional output .wav path"
        }
      },
      required: ["code"]
    }
  },
  {
    name: "infinite_process_field_effect",
    description: "process audio through an inline field effect per-sample dsp filter or wavefolder kernel",
    inputSchema: {
      type: "object",
      properties: {
        code: {
          type: "string",
          description: "field audio effect code (e.g. 'out = tanh(in * 2.5) * 0.7;')"
        },
        duration: {
          type: "number",
          description: "duration in seconds"
        },
        out_wav: {
          type: "string",
          description: "optional output .wav path"
        }
      },
      required: ["code"]
    }
  },
  {
    name: "infinite_render_sketch3d",
    description: "render procedural 3d geometry generated via algorithmic javascript to a png frame",
    inputSchema: {
      type: "object",
      properties: {
        code: {
          type: "string",
          description: "javascript 3d sketch code (e.g. 'function draw(t) { rotateY(t * 0.8); fill(0.2, 0.6, 0.9); torus(1.2, 0.4, 24, 24); }')"
        },
        out_png: {
          type: "string",
          description: "output png file path"
        },
        width: {
          type: "number",
          description: "frame width in pixels (default 720)"
        },
        height: {
          type: "number",
          description: "frame height in pixels (default 720)"
        }
      },
      required: ["code"]
    }
  },
  {
    name: "infinite_render_field_primitive",
    description: "render dynamic parametric mathematical surfaces (plane, sphere, torus) via field primitive equations",
    inputSchema: {
      type: "object",
      properties: {
        code: {
          type: "string",
          description: "field 3d primitive equation (e.g. 'P = vec3(uv.x - 0.5, sin(length(uv)*5.0 - t)*0.3, uv.y - 0.5); Cd = vec3(0.9, 0.4, 0.2);')"
        },
        topology: {
          type: "number",
          description: "0 for plane grid, 1 for sphere, 2 for torus"
        },
        out_png: {
          type: "string",
          description: "output png file path"
        }
      },
      required: ["code"]
    }
  },
  {
    name: "infinite_render_field_modifier",
    description: "deform procedural 3d mesh topology using inline field deformation expressions",
    inputSchema: {
      type: "object",
      properties: {
        code: {
          type: "string",
          description: "field mesh deformer equation (e.g. 'P.y += sin(P.x * 3.0 + t) * 0.25; Cd = vec3(0.96, 0.5, 0.4);')"
        },
        out_png: {
          type: "string",
          description: "output png file path"
        }
      },
      required: ["code"]
    }
  },
  {
    name: "infinite_render_field_pixel",
    description: "render an audio-reactive or procedural pixel shader using infinite field pixel node to a png frame",
    inputSchema: {
      type: "object",
      properties: {
        code: {
          type: "string",
          description: "field pixel shader code (e.g. 'col = vec3(uv.x, uv.y, sin(t)*0.5+0.5);')"
        },
        out_png: {
          type: "string",
          description: "output png file path"
        }
      },
      required: ["code"]
    }
  },
  {
    name: "infinite_validate_patch",
    description: "validate an infinite modular patch text file against the c++ compiler",
    inputSchema: {
      type: "object",
      properties: {
        patch_text: {
          type: "string",
          description: "content of an .inf patch file"
        }
      },
      required: ["patch_text"]
    }
  },
  {
    name: "infinite_render_video",
    description: "render an mp4 video from an infinite patch file headlessly",
    inputSchema: {
      type: "object",
      properties: {
        patch_path: {
          type: "string",
          description: "path to .inf patch file"
        },
        out_mp4: {
          type: "string",
          description: "output mp4 file path"
        },
        duration: {
          type: "number",
          description: "duration in seconds (default 5.0)"
        },
        fps: {
          type: "number",
          description: "frames per second (default 30)"
        }
      },
      required: ["patch_path", "out_mp4"]
    }
  },
  {
    name: "infinite_compare_frames",
    description: "Compare two pictures as plain facts and say whether they match. Each side is a .png path, an .inf patch path, or patch text; patches are rendered at 128x128, t=0.5 s. Returns match yes/no, the differences in words (brightness, contrast, colour, opacity, fine detail, tone shape, layout), and change tokens you can pass straight to infinite_find_knob. Use it to check a render against a target before saying it is done.",
    inputSchema: {
      type: "object",
      properties: {
        a: { type: "string", description: "first picture: png path, .inf path or patch text (e.g. the target)" },
        b: { type: "string", description: "second picture: png path, .inf path or patch text (e.g. your render)" }
      },
      required: ["a", "b"]
    }
  },
  {
    name: "infinite_find_knob",
    description: "Find which node and param move makes a wanted picture change, from measured renders (not guesses). Give `want` (e.g. [\"detail down large\", \"luma up\"]; facts: luma, contrast, spread, red, green, blue, warmth, saturation, black, white, alpha, detail, tones; levels small/medium/large optional) or `before` + `after` pictures (png, .inf or patch text) to work out the change. Returns the top moves with value, patch line and their measured effect; with a `before` patch the candidates are applied to it and rendered, and ranked by how close they land. Coverage: Effects nodes and the Compositing colour nodes (color adjustments, curves, exposure, keys, alpha...); `want` alone is a hint from 10 mostly grey test pictures (weak for colour); before/after with a patch is checked by render and is the reliable mode.",
    inputSchema: {
      type: "object",
      properties: {
        want: { type: "array", items: { type: "string" }, description: "wanted change tokens: '<fact> up|down [small|medium|large]' or 'tones [level]'" },
        before: { type: "string", description: "current picture (png, .inf or patch text); with `after`, the change is measured" },
        after: { type: "string", description: "wanted picture (png, .inf or patch text)" },
        picture_kind: { type: "string", enum: ["colour", "grey"], description: "kind of picture you are changing (default: worked out from `before`, else all pictures)" },
        node_types: { type: "array", items: { type: "string" }, description: "only consider these node types (e.g. the ones already in your patch)" },
        k: { type: "number", description: "how many suggestions (default 5)" }
      }
    }
  },
  {
    name: "infinite_node_effects",
    description: "Measured effect table for Effects nodes and Compositing colour nodes: what adding a node at defaults does to a picture, and what each param does moved up or down (measured on 10 source pictures at 128x128). Omit node_type to list every measured type with its add effect.",
    inputSchema: {
      type: "object",
      properties: {
        node_type: { type: "string", description: "node type, e.g. 'gaussianblur', 'Effects gaussianblur' or 'color adjustments'" }
      }
    }
  }
];

function handleToolCall(name, args) {
  if (name === "infinite_compare_frames") return effects.compare(args.a, args.b);
  if (name === "infinite_node_effects") return effects.nodeEffects(args.node_type);
  if (name === "infinite_find_knob") {
    let want, bad = [], measured = null;
    if (args.want && args.want.length) ({ want, bad } = effects.parseWant(args.want));
    else if (args.before && args.after) {
      measured = effects.compare(args.before, args.after);
      want = { ...measured.change_tokens };
    } else throw new Error("give `want` tokens, or both `before` and `after` pictures");
    if (!Object.keys(want).length) return { suggestions: [], note: measured ? "before and after already match on every measured fact" : `no usable tokens: ${bad.join(", ")}` };
    const k = args.k || 5;
    let kind = args.picture_kind || null;
    if (!kind && measured) kind = measured.facts.saturation.a > 0.03 ? "colour" : "grey";
    let suggestions = effects.findKnob(want, { nodeTypes: args.node_types, k: measured ? Math.max(k, 10) : k, full: !!measured, kind });
    let verified = false;
    const beforeText = measured && (() => { try { const t = require("./measure").patchFor(args.before); return /\.(png|jpe?g|webp|tiff?|bmp|exr|hdr)$/i.test(args.before) ? null : t; } catch (e) { return null; } })();
    if (beforeText) {
      const v = effects.verify(beforeText, measured.facts_b, suggestions);
      if (v.length) {
        const seen = new Set(v.map(c => c.patch_line + c.node_type));
        suggestions = [...v, ...suggestions.filter(c => !seen.has(c.patch_line + c.node_type))];
        verified = true;
      }
    }
    if (measured) delete measured.facts_b;
    return {
      wanted: want, ignored: bad, picture_kind: kind || "all pictures", measured_change: measured ? measured.differences : undefined,
      verified_by_render: verified, suggestions: suggestions.slice(0, k),
      note: (verified ? "Each suggestion was applied to the before patch and rendered; rendered_distance 0 = lands on the after picture. " : "")
        + "score = how often (share of pictures of that kind) the move caused each wanted change, minus side effects. Patch lines go inside the node block."
    };
  }

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "infinite-"));
  const binary = findBinary();

  if (name === "infinite_synthesize_field_synth") {
    const esc = escapeCode(args.code);
    const patch = `infinite-patch 1
node 1 Notes Random Note Generator
  id notes
end
node 2 Synths Field Synth
  id fsynth
  s code ${esc}
end
node 3 Utility Audio Out
  id speaker
end
note fsynth 0 notes
aud speaker 0 fsynth
`;
    const patchPath = path.join(tmpDir, `fsynth_${Date.now()}.inf`);
    const outWav = args.out_wav || path.join(tmpDir, `fsynth_${Date.now()}.wav`);
    const sumPath = path.join(tmpDir, `fsynth_${Date.now()}_sum.json`);
    fs.writeFileSync(patchPath, patch, "utf-8");

    const dur = String(args.duration || 3);
    const res = runCommand(["--audio-summary", patchPath, sumPath, "--wav", outWav, "--duration", dur]);
    return {
      status: res.ok === false ? "error" : "success",
      wav_path: outWav,
      patch_path: patchPath,
      result: res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_synthesize_field_notes") {
    const esc = escapeCode(args.code);
    const synthNode = args.synth_type === "fm" ? "Synths FM" : "Synths Wavetable";
    const patch = `infinite-patch 1
node 1 Notes Field Notes
  id fnotes
  s code ${esc}
end
node 2 ${synthNode}
  id synth
end
node 3 Utility Audio Out
  id speaker
end
note synth 0 fnotes
aud speaker 0 synth
`;
    const patchPath = path.join(tmpDir, `fnotes_${Date.now()}.inf`);
    const outWav = args.out_wav || path.join(tmpDir, `fnotes_${Date.now()}.wav`);
    const sumPath = path.join(tmpDir, `fnotes_${Date.now()}_sum.json`);
    fs.writeFileSync(patchPath, patch, "utf-8");

    const dur = String(args.duration || 3);
    const res = runCommand(["--audio-summary", patchPath, sumPath, "--wav", outWav, "--duration", dur]);
    return {
      status: res.ok === false ? "error" : "success",
      wav_path: outWav,
      result: res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_process_field_effect") {
    const esc = escapeCode(args.code);
    const patch = `infinite-patch 1
node 1 Notes Random Note Generator
  id notes
end
node 2 Synths Wavetable
  id synth
end
node 3 AudioEffects Field Effect
  id fx
  s code ${esc}
end
node 4 Utility Audio Out
  id speaker
end
note synth 0 notes
aud fx 0 synth
aud speaker 0 fx
`;
    const patchPath = path.join(tmpDir, `feffect_${Date.now()}.inf`);
    const outWav = args.out_wav || path.join(tmpDir, `feffect_${Date.now()}.wav`);
    const sumPath = path.join(tmpDir, `feffect_${Date.now()}_sum.json`);
    fs.writeFileSync(patchPath, patch, "utf-8");

    const dur = String(args.duration || 3);
    const res = runCommand(["--audio-summary", patchPath, sumPath, "--wav", outWav, "--duration", dur]);
    return {
      status: res.ok === false ? "error" : "success",
      wav_path: outWav,
      result: res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_render_sketch3d") {
    const esc = escapeCode(args.code);
    const w = args.width || 720;
    const h = args.height || 720;
    const patch = `infinite-patch 1
node 1 3D Sketch 3D
  id sk3d
  s code ${esc}
end
node 2 3D Render 3D
  id r3d
  f width ${w}
  f height ${h}
end
node 3 Utility Output
  id out
end
geo r3d 0 sk3d
cable out 0 r3d
`;
    const patchPath = path.join(tmpDir, `sk3d_${Date.now()}.inf`);
    const outPng = args.out_png || path.join(tmpDir, `sk3d_${Date.now()}.png`);
    fs.writeFileSync(patchPath, patch, "utf-8");

    const res = runCommand(["--frame", patchPath, "1.0", outPng]);
    return {
      status: res.ok === false ? "error" : "success",
      frame_path: outPng,
      result: res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_render_field_primitive") {
    const esc = escapeCode(args.code);
    const top = args.topology || 0;
    const patch = `infinite-patch 1
node 1 3D Field Primitive
  id fprim
  i topology ${top}
  s code ${esc}
end
node 2 3D Render 3D
  id r3d
  f width 720
  f height 720
end
node 3 Utility Output
  id out
end
geo r3d 0 fprim
cable out 0 r3d
`;
    const patchPath = path.join(tmpDir, `fprim_${Date.now()}.inf`);
    const outPng = args.out_png || path.join(tmpDir, `fprim_${Date.now()}.png`);
    fs.writeFileSync(patchPath, patch, "utf-8");

    const res = runCommand(["--frame", patchPath, "1.0", outPng]);
    return {
      status: res.ok === false ? "error" : "success",
      frame_path: outPng,
      result: res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_render_field_modifier") {
    const esc = escapeCode(args.code);
    const patch = `infinite-patch 1
node 1 3D Sphere
  id ball
end
node 2 3D Field Modifier
  id warp
  s code ${esc}
end
node 3 3D Render 3D
  id r3d
  f width 720
  f height 720
end
node 4 Utility Output
  id out
end
geo warp geo ball
geo r3d geo_a warp
cable out in r3d
`;
    const patchPath = path.join(tmpDir, `fmod_${Date.now()}.inf`);
    const outPng = args.out_png || path.join(tmpDir, `fmod_${Date.now()}.png`);
    fs.writeFileSync(patchPath, patch, "utf-8");

    const res = runCommand(["--frame", patchPath, "1.0", outPng]);
    return {
      status: res.ok === false ? "error" : "success",
      frame_path: outPng,
      result: res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_render_field_pixel") {
    const esc = escapeCode(args.code);
    const patch = `infinite-patch 1
node 1 Source FieldPixel
  id fp
  s code ${esc}
end
node 2 Utility Output
  id out
end
cable out in fp
`;
    const patchPath = path.join(tmpDir, `fpixel_${Date.now()}.inf`);
    const outPng = args.out_png || path.join(tmpDir, `fpixel_${Date.now()}.png`);
    fs.writeFileSync(patchPath, patch, "utf-8");

    const res = runCommand(["--frame", patchPath, "1.0", outPng]);
    return {
      status: res.ok === false ? "error" : "success",
      frame_path: outPng,
      result: res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_validate_patch") {
    if (typeof args.patch_text !== "string" || !args.patch_text) throw new Error("patch_text is required");
    const patchPath = path.join(tmpDir, "validate.inf");
    fs.writeFileSync(patchPath, args.patch_text, "utf-8");
    const res = runCommand(["--validate", patchPath]);
    return {
      ...res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_render_video") {
    if (!args.patch_path || !args.out_mp4) throw new Error("patch_path and out_mp4 are required");
    const dur = String(args.duration || 5);
    const fps = String(args.fps || 30);
    const res = runCommand(["--render", args.patch_path, args.out_mp4, "--duration", dur, "--fps", fps], 600000);
    return {
      status: res.ok === false ? "error" : "success",
      video_path: args.out_mp4,
      result: res,
      attribution: ATTRIBUTION
    };
  }

  throw new Error(`unknown tool: ${name}`);
}

function startServer() {
  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    terminal: false
  });

  rl.on("line", (line) => {
    line = line.trim();
    if (!line) return;

    let req = null;
    try {
      req = JSON.parse(line);
    } catch (e) {
      return;
    }

    const { id, method, params } = req;

    if (method === "initialize") {
      const resp = {
        jsonrpc: "2.0",
        id,
        result: {
          protocolVersion: "2024-11-05",
          capabilities: {
            tools: {}
          },
          serverInfo: {
            name: "infinite-engine",
            version: "1.0.1"
          },
          instructions: SERVER_INSTRUCTIONS
        }
      };
      process.stdout.write(JSON.stringify(resp) + "\n");
      return;
    }

    if (method === "tools/list") {
      const resp = {
        jsonrpc: "2.0",
        id,
        result: {
          tools: TOOLS
        }
      };
      process.stdout.write(JSON.stringify(resp) + "\n");
      return;
    }

    if (method === "tools/call") {
      try {
        const result = handleToolCall(params.name, params.arguments || {});
        // Every result carries the credit, even a tool that forgot to add it.
        if (result && typeof result === "object" && !result.attribution) result.attribution = ATTRIBUTION;
        const resp = {
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: JSON.stringify(result, null, 2)
              },
              {
                type: "text",
                text: "Credit: " + CREDIT_LINE + ". Mention this when you deliver the work."
              }
            ]
          }
        };
        process.stdout.write(JSON.stringify(resp) + "\n");
      } catch (err) {
        const resp = {
          jsonrpc: "2.0",
          id,
          error: {
            code: -32603,
            message: err.message
          }
        };
        process.stdout.write(JSON.stringify(resp) + "\n");
      }
      return;
    }

    // fallback empty result for notifications
    if (id !== undefined) {
      process.stdout.write(JSON.stringify({ jsonrpc: "2.0", id, result: {} }) + "\n");
    }
  });
}

module.exports = {
  startServer,
  handleToolCall,
  TOOLS
};
