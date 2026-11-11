# Next session prompt

In /Users/namansoni/Infinite/infinte-base there is uncommitted 3D render work: fur, guides, live export
supersample. Read the Status section of docs/plans/fur/README.md, the results in tools/blender-ab/README.md, and
docs/plans/realtime-engine/README.md first.

Goal: close every known gap between Infinite's three engines (GL raster, live Metal RT, settle path tracer) and
Blender Cycles.

Rules:
- Work in three blocks, one branch per block, commit per step when I say so.
- Track progress in one place: a table at the top of docs/plans/fur/README.md.
- Builds only via `python3 scripts/ibuild.py build --mode release`, one at a time.
- A/B every change with tools/blender-ab/harness.py, and keep a change only if it measures better.

Block A: shared correctness (affects every scene)
1. Scene tint. Infinite is +6% R, +11% G, +15% B against Cycles on `bed_close`, which has no fur. Find the cause
   in world or sun colour, tonemap, or colour space. Done = under 2% per channel in live and settle.
2. Settle `bed` is too dark under a grazing sun (brightness 0.70). Done = within 5%.
3. Raster over-lighting. The boat is 1.8 to 4.4 times too bright, interior FLIP was 0.83, and raster has no GI.
   Bring raster within reach of Cycles brightness (an ambient or probe term).
4. Raster fur. Ribbons are back-face culled (draw them two-sided). The shadow map is coarser than 5 mm fur, which
   leaves blotches. Done = `bed_fur_close` raster FLIP well under 0.40, with no blotches on the board.

Block B: engine quality and speed
5. Live: GI fireflies, the glitter blob, the hard sun shadow edge (soft sun angle), and true displacement.
6. Settle: sheen and coat on bounces, and alpha (cutout) in shadow rays.
7. Export time. A 1-minute film takes about 3.4 h against a 15 min gate, and the R4 FLIP of 0.05 holds on only
   6 of 9 cases. Dense `hair_long` (length 0.3, gravity 0.6, 16 passes) timed out the live render at 600 s.
   Profile first, then fix the biggest cost.

Block C: materials and finishing
8. Hair BSDF (Block 3 of the fur plan):
   - Material gets a Hair model with Chiang lobes and melanin.
   - Fur gets the Tangent normals mode and its warning line.
   - Build it in all three engines.
   - A/B against Cycles Principled Hair (`hair_melanin` sweep).
9. Real subsurface scattering to replace wrap lighting (UtilityNodes.h:348), plus thin back-lit translucency
   (sails, leaves).
10. Fur paperwork:
   - Restate the README gate against today's numbers.
   - Add the fur results, the `fur`, `detail` and `region` scene keys, and the bed_fur, bed_fur_close and
     hair_long cases to tools/blender-ab/README.md.
   - Baseline those cases in baseline.json.
   - Regenerate scripts/node_vocab.py.
   - Add Fur to the HelpWindows.cpp Procedural Operations list, AgentDocs.h, AISkillContent.h,
     tests/chat/find_nodes.tsv, tools/manual/type_categories.json and resources/lang/*.tsv.
   - Add help for Material detailScale and detailStrength, and Mapping breakup.
   - Add a small Fur selftest covering the cap, determinism, selection, guide, and save/load.
   - Replace the hard-coded `i op 22` in harness.py.
11. Write the design doc for the custom Metal/Vulkan real-time photoreal engine, using what Blocks A and B taught
    us. Doc only, no code.

Finish with a full live and settle sweep over every case against baseline.json, so no case regresses.
