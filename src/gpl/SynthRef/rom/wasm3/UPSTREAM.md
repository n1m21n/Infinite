# wasm3 (vendored)

- Source: https://github.com/wasm3/wasm3 tag v0.9.0 (2026-08-24), source/ only
- Licence: MIT (LICENSE, Steven Massey, Volodymyr Shymanskyy)
- Not vendored: m3_api_* (WASI, libc, tracer: no Synth REF ROM imports anything), extra/, extensions/.
- Unmodified. Synth REF builds it with -Dd_m3MaxNativeStack=(512*1024) (a trap, not a crash, past 512 KB of native stack).
