const fs = require("fs");
const path = require("path");
const os = require("os");
const readline = require("readline");
const { ATTRIBUTION, findBinary, escapeCode, runCommand } = require("./engine");

const TOOLS = [
  {
    name: "infinite_synthesize_field_synth",
    description: "synthesize 24-bit 48khz audio from an inline field synth per-sample dsp kernel using infinite c++ engine",
    inputSchema: {
      type: "object",
      properties: {
        code: {
          type: "string",
          description: "field dsp code (e.g. 'out = sin(phase * 6.283185); phase = fract(phase + freq/sr);')"
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
          description: "field notes logic (e.g. 'if (tick(1/4) > 0.5) { note(60, 0.8, 1/4) }')"
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
    description: "render procedural 3d geometry generated via algorithmic javascript to a high-resolution png frame",
    inputSchema: {
      type: "object",
      properties: {
        code: {
          type: "string",
          description: "javascript 3d sketch code (e.g. 'function setup(){ background(10,12,20); } function draw(){ rotateY(t); torus(2.0, 0.6); }')"
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
          description: "frames per second (default 60)"
        }
      },
      required: ["patch_path", "out_mp4"]
    }
  }
];

function handleToolCall(name, args) {
  const tmpDir = os.tmpdir();
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
      status: "success",
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
      status: "success",
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
      status: "success",
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
      status: "success",
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
      status: "success",
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
      status: "success",
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
      status: "success",
      frame_path: outPng,
      result: res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_validate_patch") {
    const patchPath = path.join(tmpDir, `val_${Date.now()}.inf`);
    fs.writeFileSync(patchPath, args.patch_text, "utf-8");
    const res = runCommand(["--validate", patchPath]);
    return {
      ...res,
      attribution: ATTRIBUTION
    };
  }

  if (name === "infinite_render_video") {
    const dur = String(args.duration || 5);
    const fps = String(args.fps || 60);
    const res = runCommand(["export-video", "--graph", args.patch_path, "--out", args.out_mp4, "--duration", dur, "--fps", fps]);
    return {
      status: "success",
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
            version: "0.6.0"
          }
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
        const resp = {
          jsonrpc: "2.0",
          id,
          result: {
            content: [
              {
                type: "text",
                text: JSON.stringify(result, null, 2)
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
