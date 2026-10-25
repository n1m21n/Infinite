# SLOOP SMK upstream

Vendored by `scripts/synthref_vendor_sloop_smk.py`; do not edit files under `upstream/` or `generated/`.

- Source: https://github.com/ChanceTheMaker/sloop-smk (branch integrate/sloop-2.6-smk)
- Commit: `08c823081d9ab394077767a0604df7799ad449b0` (its `sloop/` is SLOOP 2.8 from github.com/isod89/sloop-fm1 b77bf55)
- Vendored: 2026-10-24
- Licences: GPL-3.0-only (SLOOP / Felucca, Leo Kuroshita and contributors; SMK port), Apache-2.0
  (sloop/firmware/src/fm6_core.c, msfa), MIT (phys_dsp.c, phys_symp.c: DaisySP / Rings), OFL (font in
  generated/felucca_font.h), CC0 (samples in generated/felucca_samples.h). See upstream/LICENSING.md.
- Not vendored: the "all rights reserved" Felucca assets (icons.png, panel art, Hugelton sample pack).
  generated/felucca_icons.h comes from a blank atlas, so the knob-card icons are empty.
- generated/: the output of the firmware's own sloop/tools/gen_*.py (read before running, run with -I).

89 files (SHA-256 prefix):

| File | SHA-256 |
|---|---|
| `upstream/firmware/hal/fm1_cc.h` | `3a4a44eb638b5e0e` |
| `upstream/firmware/hal/fm1_flash.h` | `232ae3507c31a899` |
| `upstream/firmware/hal/fm1_xip.h` | `48e0ee6ab2ff94ba` |
| `upstream/firmware/hal/sloop_smk_storage_map.h` | `83206e6d4785801c` |
| `upstream/firmware/hal/smk37_flash_profile.h` | `4038da9f2c85dfce` |
| `upstream/firmware/hal/smk37_pad_feedback.h` | `c30137d89ea61037` |
| `upstream/firmware/hal/smk37_public_profile.h` | `5a93bf39f8455faf` |
| `upstream/firmware/hal/smk37_sample_store.h` | `72d6cb2f7e7a256a` |
| `upstream/firmware/hal/smk37_store.h` | `decf477acccff555` |
| `upstream/firmware/hal/smk37_trim.h` | `6eea6c0532034ea1` |
| `upstream/firmware/src/sloop_smk_cpu1.h` | `e7690381e1b91351` |
| `upstream/firmware/src/sloop_smk_editor.c` | `9ad218359959a516` |
| `upstream/firmware/src/sloop_smk_editor_samples.c` | `64f5b976b0e1a25c` |
| `upstream/firmware/src/sloop_smk_runtime.c` | `09b174c28f5b816c` |
| `upstream/firmware/src/sloop_smk_samples.c` | `cd4c8e8039cecdd0` |
| `upstream/firmware/src/sloop_smk_storage.c` | `b235449ae0397eba` |
| `upstream/firmware/src/sloop_smk_storage_policy.c` | `df593db3fe5efd6c` |
| `upstream/firmware/src/sloop_smk_ui.c` | `576f7d8a90574b53` |
| `upstream/sloop/firmware/hal/fm1_cc.h` | `3a4a44eb638b5e0e` |
| `upstream/sloop/firmware/hal/fm1_usb.h` | `05988a86135b7c49` |
| `upstream/sloop/firmware/hal/fm1_xip.h` | `48e0ee6ab2ff94ba` |
| `upstream/sloop/firmware/src/arranger.c` | `13672dc7dacc2dff` |
| `upstream/sloop/firmware/src/arranger.h` | `46c725df1b9be778` |
| `upstream/sloop/firmware/src/arranger_scene.c` | `c225e2d8c14a8064` |
| `upstream/sloop/firmware/src/bootguard.h` | `b65a4d414d86fbdb` |
| `upstream/sloop/firmware/src/core.h` | `469d50ce5d788478` |
| `upstream/sloop/firmware/src/cpu1.h` | `6a37b6066654505a` |
| `upstream/sloop/firmware/src/drum_synth.c` | `9d606f94bfc75454` |
| `upstream/sloop/firmware/src/drums.c` | `f58d409b1353da13` |
| `upstream/sloop/firmware/src/dsp.c` | `fc54135f55cd3d5b` |
| `upstream/sloop/firmware/src/editor.c` | `a86a3172e60e9812` |
| `upstream/sloop/firmware/src/editor_dsyn.c` | `e349d2282eaa53a3` |
| `upstream/sloop/firmware/src/editor_fm6.c` | `523ea7c571863e4c` |
| `upstream/sloop/firmware/src/editor_live.c` | `202e4c3d249ff152` |
| `upstream/sloop/firmware/src/eng_analog.c` | `d7c8492086da8329` |
| `upstream/sloop/firmware/src/eng_digital.c` | `8b4bcb85a0ea53fe` |
| `upstream/sloop/firmware/src/eng_drawbar.c` | `fa2fab1dfbd3dd59` |
| `upstream/sloop/firmware/src/eng_fm6.c` | `0414e8c0080006bf` |
| `upstream/sloop/firmware/src/eng_formant.c` | `472b9e88faaec24f` |
| `upstream/sloop/firmware/src/eng_grain.c` | `de1abc735738438b` |
| `upstream/sloop/firmware/src/eng_lofi.c` | `23b4129343c36ff0` |
| `upstream/sloop/firmware/src/eng_noise.c` | `6ae4843d0500c731` |
| `upstream/sloop/firmware/src/eng_phase.c` | `03355831391e7a4c` |
| `upstream/sloop/firmware/src/eng_phys.c` | `476d5972055d2940` |
| `upstream/sloop/firmware/src/eng_sample.c` | `549e8f90dbe041da` |
| `upstream/sloop/firmware/src/eng_slice.c` | `c0392c80d495eded` |
| `upstream/sloop/firmware/src/eng_trio.c` | `08b675796e0a1060` |
| `upstream/sloop/firmware/src/engines.c` | `fd30dbffc946bd39` |
| `upstream/sloop/firmware/src/fm6_bank.c` | `811ba48a0d1abcfe` |
| `upstream/sloop/firmware/src/fm6_core.c` | `56364d93ecd23c9c` |
| `upstream/sloop/firmware/src/fx.c` | `5e8fa9f80d4d55e7` |
| `upstream/sloop/firmware/src/gfx.c` | `1c4c2ff2cd6ab2ad` |
| `upstream/sloop/firmware/src/icons.c` | `9cee5b22ff507f8f` |
| `upstream/sloop/firmware/src/libc.c` | `cf5f101bf8495630` |
| `upstream/sloop/firmware/src/panel.c` | `05898938d9067fe2` |
| `upstream/sloop/firmware/src/params.c` | `6e0f063e2b83e4a4` |
| `upstream/sloop/firmware/src/phys_dsp.c` | `0992d8fa76e35984` |
| `upstream/sloop/firmware/src/phys_symp.c` | `93d00aa99ecd671a` |
| `upstream/sloop/firmware/src/preset_trim.h` | `a13aebb34ab05b2f` |
| `upstream/sloop/firmware/src/project.c` | `07f137bd6dadf480` |
| `upstream/sloop/firmware/src/punch.c` | `2abfe630268a7970` |
| `upstream/sloop/firmware/src/scratch.h` | `b1abe9a3f0eddce8` |
| `upstream/sloop/firmware/src/seq.c` | `707c88dad4cb90bb` |
| `upstream/sloop/firmware/src/slicer.c` | `336c0b3f8fbf1491` |
| `upstream/sloop/firmware/src/storage.c` | `cc36953d0d2f2b4a` |
| `upstream/sloop/firmware/src/uac_fir.h` | `deb62ebb4a72b96d` |
| `upstream/sloop/firmware/src/ui.c` | `d75551db117a53dd` |
| `upstream/sloop/firmware/src/ui_draw.c` | `7351b7163375add4` |
| `upstream/sloop/firmware/src/ui_input.c` | `5ab04316c611db7d` |
| `upstream/sloop/firmware/src/ui_layers.c` | `4bf5b832e7756be7` |
| `upstream/sloop/firmware/src/ui_menu.c` | `8060d07c15e9f37e` |
| `upstream/sloop/firmware/src/ui_song.c` | `4244be2e55926153` |
| `upstream/sloop/firmware/src/ui_studio.c` | `6512b79a28166341` |
| `upstream/sloop/firmware/src/ui_vis.c` | `035cfc190c19f2f8` |
| `upstream/sloop/firmware/src/upreset.c` | `5778bd9900d06c00` |
| `upstream/sloop/firmware/src/usb.c` | `21442ecdcd459db0` |
| `upstream/sloop/firmware/src/voice.c` | `7fe4405cf127ceb7` |
| `upstream/web/audio/sloop-smk-engine.c` | `396952ad051372f6` |
| `upstream/LICENSE` | `230184f60bae2fea` |
| `upstream/LICENSING.md` | `31b51266cf3a5acb` |
| `upstream/README.md` | `cef160c6b3f33408` |
| `upstream/sloop/LICENSE` | `230184f60bae2fea` |
| `upstream/sloop/LICENSING.md` | `e35db108f2d7df06` |
| `generated/felucca_drumkits.h` | `fb75f4fb3d378659` |
| `generated/felucca_fm6.h` | `ebc6bba5d4e44e5b` |
| `generated/felucca_font.h` | `201bc7456fd3e2be` |
| `generated/felucca_icons.h` | `b85642e8ce743d73` |
| `generated/felucca_samples.h` | `7a2846fd172b5ac4` |
| `generated/felucca_tables.h` | `8a0540c567e4f9b8` |
