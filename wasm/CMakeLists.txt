# infinite_gen_wasm: Generate Samples' real engine (SampleStyles -> OfflineGraph -> Master) for the website.
# Build: source ~/emsdk/emsdk_env.sh && emcmake cmake -S wasm -B build-wasm -DCMAKE_BUILD_TYPE=Release && cmake --build build-wasm -j2
cmake_minimum_required(VERSION 3.20)
project(infinite_gen_wasm CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(R ${CMAKE_CURRENT_SOURCE_DIR}/..)

file(GLOB DSP_SRC ${R}/src/audio/dsp/*.cpp)
file(GLOB FIELD_SRC ${R}/src/core/field/*.cpp)
set(SRC
    ${R}/src/app/graph/OfflineGraph.cpp
    ${R}/src/app/graph/SampleStyles.cpp
    ${R}/src/audio/AudioEngine.cpp
    ${R}/src/audio/AudioVoice.cpp
    ${R}/src/audio/Wavetable.cpp
    ${R}/src/audio/EffectDefs.cpp
    ${R}/src/audio/ParamMailbox.cpp
    ${R}/src/audio/NoteModel.cpp
    ${R}/src/audio/AudioWorkerPool.cpp
    ${R}/src/audio/MeterRing.cpp
    ${DSP_SRC}
    ${FIELD_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_writer_stub.cpp
    ${R}/src/core/CookProbe.cpp
    ${R}/src/core/Modulation.cpp
    ${R}/src/nodes/WavetableNode.cpp
    ${R}/src/nodes/AnalogNode.cpp
    ${R}/src/nodes/AudioEffectNode.cpp
    ${R}/src/nodes/DrumSequencerNode.cpp
    ${R}/src/nodes/FMNode.cpp
    ${R}/src/nodes/FieldSynthNode.cpp
    ${R}/src/nodes/GranularNode.cpp
    ${R}/src/nodes/MetallicNode.cpp
    ${R}/src/nodes/NoteNodes.cpp
    ${R}/src/nodes/OscillatorNode.cpp
    ${R}/src/nodes/PartialsNode.cpp
    ${R}/src/nodes/SamplerNode.cpp
    ${R}/src/nodes/ImageSpectralSynthNode.cpp
    ${R}/src/nodes/AudioNodes.cpp
    ${R}/src/core/Patch.cpp
    ${R}/src/core/NodeFactory.cpp
    ${R}/src/core/AudioSummary.cpp
    ${R}/src/core/Transport.cpp
    ${R}/src/core/AudioDecodeCache.cpp
    ${R}/src/core/GLUtil.cpp
    ${R}/src/platform/common/MediaDecodePortable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_userstyle_stub.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_main.cpp)

add_executable(infinite_gen_wasm ${SRC})
target_include_directories(infinite_gen_wasm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim ${R}/src ${R}/src/core ${R}/src/nodes ${R}/src/platform ${R}/src/audio
    ${R}/external ${R}/external/json ${R}/external/stb)
target_compile_options(infinite_gen_wasm PRIVATE -O2 -fwasm-exceptions -sUSE_GLFW=3)
target_link_options(infinite_gen_wasm PRIVATE
    -O2 -fwasm-exceptions -sUSE_GLFW=3 -sMODULARIZE=1 -sEXPORT_NAME=InfiniteGen -sENVIRONMENT=worker,web,node
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=128MB -sFILESYSTEM=1 -sEXPORT_ES6=0
    "-sEXPORTED_FUNCTIONS=['_gen_style_count','_gen_style_id','_gen_scale_count','_gen_scale_name','_gen_run','_gen_set_shape','_gen_pcm','_gen_label','_gen_patch','_gen_bpm','_gen_attempt','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','HEAPF32','FS']"
    --preload-file ${R}/assets/drumkits/infinite-basic@/kit)
