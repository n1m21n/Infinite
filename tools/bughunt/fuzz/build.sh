#!/usr/bin/env bash
# Builds the decoder fuzz targets in tools/bughunt/fuzz/ with ASan + UBSan.
# These link a handful of sources directly, not the app, so they build in
# seconds and don't count against the one-app-build-at-a-time rule.
#
#   tools/bughunt/fuzz/build.sh [OUT_DIR] [target ...]     (default: build-fuzz, all)
#
# With a clang that ships libFuzzer (Linux, Homebrew llvm) it links real
# libFuzzer (-fsanitize=fuzzer). Apple clang has none, so there it links
# driver.cpp, a small coverage-guided loop over the same harness entry point.
# Set CXX to pick a compiler; FUZZ_ENGINE=driver forces the fallback.
#
# Run (both engines take the same basics):
#   build-fuzz/fuzz_hap build-fuzz/corpus/hap -max_total_time=60 -artifact_prefix=build-fuzz/
#   build-fuzz/fuzz_hap build-fuzz/crash-<hash>         # replay one input
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
HERE="$ROOT/tools/bughunt/fuzz"
OUT="${1:-$ROOT/build-fuzz}"
shift || true
TARGETS=("$@")
[ ${#TARGETS[@]} -eq 0 ] && TARGETS=(hap patch gltf audio_decode)
mkdir -p "$OUT/obj"

if [ -z "${CXX:-}" ]; then
   if [ "$(uname)" = Darwin ]; then CXX="xcrun clang++"; else CXX=clang++; fi
fi
CC="${CXX/clang++/clang}"
SAN="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
COMMON="-g -O1 -w $SAN"
CXXFLAGS="-std=c++20 $COMMON"

ENGINE="${FUZZ_ENGINE:-}"
if [ -z "$ENGINE" ]; then
   if echo 'extern "C" int LLVMFuzzerTestOneInput(const unsigned char*, unsigned long){return 0;}' |
      $CXX -x c++ - -fsanitize=fuzzer -o "$OUT/obj/probe" >/dev/null 2>&1; then
      ENGINE=libfuzzer
   else
      ENGINE=driver
   fi
fi
if [ "$ENGINE" = libfuzzer ]; then
   INSTR="-fsanitize=fuzzer-no-link"
   LINK="-fsanitize=fuzzer"
   DRIVER_OBJ=""
else
   INSTR="-fsanitize-coverage=trace-pc-guard"
   LINK=""
   [ "$(uname)" = Darwin ] && LINK="-Wl,-U,_LLVMFuzzerInitialize" # optional hook, weak in driver.cpp
   DRIVER_OBJ="$OUT/obj/driver.o"
   $CXX $CXXFLAGS -c "$HERE/driver.cpp" -o "$DRIVER_OBJ"
fi
echo "engine: $ENGINE  compiler: $CXX  out: $OUT"

# Third-party code is built once without coverage guards: it is not what we
# are hunting in, and its edges would drown the corpus in noise.
ext_obj() { # name source [flags...]
   local name="$1" src="$2"; shift 2
   local o="$OUT/obj/ext_$name.o"
   if [ ! -f "$o" ]; then
      case "$src" in
      *.c) $CC -g -O1 -w $SAN "$@" -c "$src" -o "$o" ;;
      *) $CXX $CXXFLAGS "$@" -c "$src" -o "$o" ;;
      esac
   fi
   echo "$o"
}

build() { # target harness-objs-and-sources...
   local t="$1"; shift
   local objs=() s
   for s in "$@"; do
      case "$s" in
      *.o) objs+=("$s") ;;
      *)
         local o="$OUT/obj/${t}_$(basename "${s%.*}").o"
         $CXX $CXXFLAGS $INSTR $INC -c "$s" -o "$o"
         objs+=("$o")
         ;;
      esac
   done
   $CXX $CXXFLAGS $LINK "${objs[@]}" $DRIVER_OBJ $LIBS -o "$OUT/fuzz_$t"
   mkdir -p "$OUT/corpus/$t"
   # checked-in seeds already carry the harness selector byte where one is used
   [ -d "$HERE/seeds/$t" ] && cp "$HERE/seeds/$t"/* "$OUT/corpus/$t/"
   echo "built $OUT/fuzz_$t"
}

seed() { # target files...
   local t="$1"; shift
   local f
   for f in "$@"; do
      [ -f "$f" ] && cp "$f" "$OUT/corpus/$t/seed_$(basename "$f")"
   done
   return 0
}

for t in "${TARGETS[@]}"; do
   LIBS=""
   case "$t" in
   hap)
      INC="-I$ROOT/src/core"
      build hap "$HERE/fuzz_hap.cpp" "$ROOT/src/core/HapReader.cpp"
      seed hap "$ROOT"/tests/data/*.mov "$ROOT"/tests/fixtures/*.mov
      ;;
   patch)
      INC="-I$ROOT/src -I$ROOT/src/core -I$ROOT/src/platform -I$ROOT/external"
      build patch "$HERE/fuzz_patch.cpp" "$ROOT/src/core/Patch.cpp" "$ROOT/src/core/BlendModes.cpp"
      # every patch the repo ships is a seed
      while IFS= read -r f; do seed patch "$f"; done < <(find "$ROOT/tests" "$ROOT/patches" "$ROOT/examples" \
         -name '*.inf' -size -64k 2>/dev/null | head -400)
      ;;
   gltf)
      INC="-I$ROOT/src/core -I$ROOT/src/platform -I$ROOT/external/cgltf"
      # cgltf reads 16/32-bit indices straight from the buffer; an odd
      # bufferView offset is a misaligned load, harmless on arm64/x86, so the
      # alignment check only stops the fuzzer at the first odd offset.
      CXXFLAGS="$CXXFLAGS -fno-sanitize=alignment" build gltf "$HERE/fuzz_gltf.cpp" "$ROOT/src/core/GltfImport.cpp"
      while IFS= read -r f; do seed gltf "$f"; done < <(find "$ROOT/tests" "$ROOT/assets" \
         \( -name '*.gltf' -o -name '*.glb' \) -size -256k 2>/dev/null | head -50)
      # the harness reads byte 0 as the gltf/glb selector
      for f in "$OUT"/corpus/gltf/seed_*; do
         [ -f "$f" ] || continue
         case "$f" in *.glb) p='\x01' ;; *) p='\x00' ;; esac
         { printf "$p"; cat "$f"; } > "$f.sel" && mv "$f.sel" "$f"
      done
      ;;
   audio_decode)
      INC="-I$ROOT/src/platform -I$ROOT/src/platform/common -I$ROOT/src/core -I$ROOT/external -I$ROOT/external/stb"
      STB=$(ext_obj stb "$HERE/stb_impl.cpp" -I"$ROOT/external/stb")
      MINIZ=$(ext_obj miniz "$ROOT/external/miniz.c" -I"$ROOT/external")
      CXXFLAGS="$CXXFLAGS -Dstrtok_s=strtok_r" build audio_decode "$HERE/fuzz_audio_decode.cpp" \
         "$ROOT/src/platform/common/MediaDecodePortable.cpp" "$STB" "$MINIZ"
      # byte 0 picks the extension: 0 wav, 1 aif, 2 aiff, 3 flac, 4 mp3
      while IFS= read -r f; do
         case "$f" in *.wav) p='\x00' ;; *.aif) p='\x01' ;; *.aiff) p='\x02' ;; *.flac) p='\x03' ;; *.mp3) p='\x04' ;; esac
         { printf "$p"; cat "$f"; } > "$OUT/corpus/audio_decode/seed_$(basename "$f")"
      done < <(find "$ROOT/tests" "$ROOT/assets" \( -name '*.wav' -o -name '*.aif' -o -name '*.aiff' \
         -o -name '*.flac' -o -name '*.mp3' \) -size -128k 2>/dev/null | head -40)
      ;;
   *) echo "unknown target $t" >&2; exit 2 ;;
   esac
done
