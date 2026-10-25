# FM1 OPEN upstream

Vendored by `scripts/synthref_vendor_mvave.py`; do not edit files under `upstream/`.

- Source: https://github.com/m-vave/fm1_open
- Commit: `4c7496daac04a95ff10d1afdcc8f9ead2dbabc41`
- Vendored: 2026-10-24
- Licences: GPL-3.0-only WITH GPL-3.0-linking-exception (app, web, sdk/include),
  Apache-2.0 (web/synth DX7 port of msfa, app/factory_dx7*.inc / fm_factory.inc), MIT (third_party/synth,
  web/synth sea_*). See the SPDX line of each file.
  See upstream/LICENSE.md and upstream/THIRD_PARTY.md.
- Not vendored: sdk/lib (proprietary JieLi engine library), sdk/firmware (stock images),
  sdk/include/libc, app/board (hardware drivers), tools.

81 files (SHA-256 prefix):

| File | SHA-256 |
|---|---|
| `app/audio_output.c` | `a3d932bf0479dc27` |
| `app/audio_output.h` | `38eb4301619684ff` |
| `app/fm_factory.c` | `64ab196546022ac0` |
| `app/fm_factory.inc` | `b3b002d998be7e93` |
| `app/fm_library.c` | `d8b8baf0365d2f2c` |
| `app/fm_patch.h` | `126cc73efd0a238b` |
| `app/instrument.h` | `635e82a3b28b47da` |
| `app/midi_router.c` | `ebffaea15fea41a1` |
| `app/presets.c` | `7b8442692d62eee4` |
| `app/project.c` | `eca6d3b304fe4ba7` |
| `app/samples.c` | `a199b04c254350c1` |
| `app/screen.c` | `1f00337738f9aa38` |
| `app/synth/bass303.h` | `47e90ae5a34f679c` |
| `app/synth/drum808.h` | `e15b18600aefb1b7` |
| `app/synth/engine.h` | `34c09dcfec237e75` |
| `app/synth/fm1_param.h` | `8fe56e84bf360b04` |
| `app/synth/sampledrum.h` | `2f0f760c4761c71f` |
| `app/track_session.inc` | `b1521fb4190b99fa` |
| `app/transfer.c` | `814b6ca73fed96ed` |
| `app/tuning_hook.h` | `1c969685f6f4b4a7` |
| `app/version.h` | `853f7410abaab591` |
| `web/engine/edit.c` | `d34972a8e5dec27c` |
| `web/engine/effects.c` | `fbf2f5475925c757` |
| `web/engine/internal.h` | `940650d2d9e4b8d9` |
| `web/engine/sequencer.c` | `bd86bdc5b02949f9` |
| `web/engine/Simulator.c` | `c6b56274d4e35503` |
| `web/engine/sound.c` | `1d8ccc15a157abde` |
| `web/synth/adsr.c` | `1514be4bfb022a92` |
| `web/synth/adsr.h` | `75e2613e2912f96c` |
| `web/synth/demo_bank.h` | `b0a7e8b1323b8330` |
| `web/synth/dx7core.c` | `ef5daf8a31ccaace` |
| `web/synth/dx7core.h` | `3dcee609f125a8e8` |
| `web/synth/dx7ctrl.h` | `673dcf6ffbbb75c4` |
| `web/synth/dx7env.c` | `056eebf3116cf43f` |
| `web/synth/dx7env.h` | `189ee063d4a5bd2a` |
| `web/synth/dx7lfo.c` | `283cdc5916c4725b` |
| `web/synth/dx7lfo.h` | `c200af6ce0089d86` |
| `web/synth/dx7math.c` | `69093924ded2ed22` |
| `web/synth/dx7math.h` | `03752d60d6ee9507` |
| `web/synth/dx7note.c` | `27f28fd09973d0c0` |
| `web/synth/dx7note.h` | `e50ee40d73eca4cf` |
| `web/synth/dx7pitchenv.c` | `34f4bca953318e4b` |
| `web/synth/dx7pitchenv.h` | `13fc9d1185d6a65d` |
| `web/synth/dx7synth.c` | `f6dd56e0f519ee03` |
| `web/synth/dx7synth.h` | `5ea6e9aaded5e3cf` |
| `web/synth/fixed.c` | `8a4812571a9dc28d` |
| `web/synth/mixer.c` | `12ad27376e97598e` |
| `web/synth/mixer.h` | `1599b877f69f39ae` |
| `web/synth/psg.c` | `2cdf5a5b993d44ab` |
| `web/synth/sample_metadata.inc` | `47cfaa9fc1062b9c` |
| `web/synth/sampledrum.c` | `dc720f9c171cca0a` |
| `web/synth/sea_decode.c` | `f869882efe51a02c` |
| `web/synth/sea_encode.h` | `73ceb8b74e21990d` |
| `web/synth/synth_dx7.h` | `7cc157025857ae42` |
| `web/synth/types.h` | `d6d0ba2f77ceb109` |
| `web/synth/voices.c` | `3a6cbfab27aa18e9` |
| `third_party/synth/bass303.c` | `edb988b6c3512a25` |
| `third_party/synth/bass303.h` | `82fb20bf30ce03a7` |
| `third_party/synth/bass303_state.h` | `bbc98118eb69fcac` |
| `third_party/synth/drum808.c` | `3fa331eac117d0c8` |
| `third_party/synth/drum808.h` | `4578387831fca632` |
| `third_party/synth/drum808_state.h` | `e33dd154f2991526` |
| `third_party/synth/dsp_math.h` | `30f887ba577e1c11` |
| `third_party/synth/emu2149.c` | `bd6f6eab1966ad83` |
| `third_party/synth/emu2149.h` | `4bfef8abef9ff40c` |
| `third_party/synth/engine.h` | `71d9d2816e335701` |
| `third_party/synth/fixed.h` | `dca8477d3be3a224` |
| `third_party/synth/pluck.c` | `3f7903c166c2feae` |
| `third_party/synth/pluck.h` | `1275e911f8736d65` |
| `third_party/synth/psg.h` | `638ef873c6b631db` |
| `third_party/synth/sampledrum.h` | `4e7692b8e7dd9c76` |
| `third_party/synth/voices.h` | `46720c9adbadb47c` |
| `sdk/include/fm1.h` | `fb19009318299d04` |
| `sdk/include/fm1_audio_io.h` | `3c82758517f004ce` |
| `sdk/include/fm1_block.h` | `25be4238581552b1` |
| `sdk/include/fm1_device.h` | `d927c7cdc0d46cfc` |
| `sdk/include/fm1_engine_control.h` | `1ff4ec2534bd4653` |
| `sdk/include/fm1_sea.h` | `ced50bc6a08ba6e2` |
| `LICENSE.md` | `fe7344af68a6e65d` |
| `THIRD_PARTY.md` | `c10451a1c0b368ec` |
| `README.md` | `3900630939e64c0c` |
