cmake_minimum_required(VERSION 3.16)

# Universal + a floor of macOS 11: UTType (file dialogs) and the modern
# AVAssetWriter APIs need 11.0, and shipping both slices means the DMG runs on
# Intel Macs as well as Apple Silicon. Must be set before project().
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(Infinite C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GLFW is built from source and linked statically rather than taken from
# Homebrew: a downloaded .app must not depend on /opt/homebrew existing.
include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

set(IMGUI_DIR external/imgui)
set(NODE_EDITOR_DIR external/imgui-node-editor)
set(STB_DIR external/stb)

add_executable(Infinite MACOSX_BUNDLE
    src/main.cpp
    src/core/GLUtil.cpp
    src/core/NodeFactory.cpp
    src/core/FilterDefs.cpp
    src/core/BlendModes.cpp
    src/core/Mesh.cpp
    src/core/Transport.cpp
    src/core/Modulation.cpp
    src/core/Patch.cpp
    src/platform/Platform.mm
    src/nodes/ImageSourceNode.cpp
    src/nodes/ShapeNode.cpp
    src/nodes/FormulaNode.cpp
    src/nodes/FilterNode.cpp
    src/nodes/BlendNode.cpp
    src/nodes/LayerStackNode.cpp
    src/nodes/FitNode.cpp
    src/nodes/VideoSourceNode.cpp
    src/nodes/ModulatorNodes.cpp
    src/nodes/NoiseNode.cpp
    src/nodes/ResynthNode.cpp
    src/nodes/MacroNodes.cpp
    src/nodes/CurvesNode.cpp
    src/nodes/RemoveBgNode.cpp
    src/nodes/DrawNode.cpp
    src/nodes/RampNode.cpp
    src/nodes/AnalyzeNodes.cpp
    src/nodes/Geometry3DNodes.cpp
    src/nodes/ModelSourceNode.cpp
    src/nodes/Text3DNode.cpp
    src/nodes/UtilityNodes.cpp
    src/nodes/PathNode.cpp
    src/nodes/OceanNode.cpp
    src/nodes/CurveNode.cpp
    src/nodes/SimulationNodes.cpp
    src/nodes/GeometryOpNodes.cpp
    src/nodes/SceneNodes.cpp
    src/nodes/FeedbackNodes.cpp
    src/nodes/SwitcherNode.cpp
    src/nodes/TextNode.cpp
    src/nodes/OutputNode.cpp

    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp

    ${NODE_EDITOR_DIR}/imgui_node_editor.cpp
    ${NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
    ${NODE_EDITOR_DIR}/imgui_canvas.cpp
    ${NODE_EDITOR_DIR}/crude_json.cpp
)

target_include_directories(Infinite PRIVATE
    src src/core src/nodes src/platform
    ${IMGUI_DIR} ${IMGUI_DIR}/backends
    ${NODE_EDITOR_DIR}
    ${STB_DIR}
)

target_link_libraries(Infinite PRIVATE glfw)

# The platform shim holds AVFoundation objects across function calls; without ARC
# the autoreleased writer input/adaptor die at the end of their @autoreleasepool
# and the stored pointers dangle.
set_source_files_properties(src/platform/Platform.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

# imgui-node-editor 0.9.1 ships its own GetKeyIndex() shim, which collides with
# ImGui's obsolete-keyio copy. Dropping the obsolete API leaves only the shim.
target_compile_definitions(Infinite PRIVATE
    IMGUI_DISABLE_OBSOLETE_KEYIO
    IMGUI_DEFINE_MATH_OPERATORS
)

if(APPLE)
    target_link_libraries(Infinite PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreText"
        "-framework CoreGraphics"
        "-framework CoreFoundation"
        "-framework AppKit"
        "-framework UniformTypeIdentifiers"
        "-framework AVFoundation"
        "-framework Vision"
        "-framework ModelIO"
        "-framework Accelerate"
        "-framework CoreMedia"
        "-framework CoreVideo"
    )
    target_compile_definitions(Infinite PRIVATE GL_SILENCE_DEPRECATION)

    set_target_properties(Infinite PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Infinite"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.namansoni.infinite"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
        MACOSX_BUNDLE_ICON_FILE "Infinite"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets/Infinite.icns"
    )

    # Rebuilding the executable inside a signed bundle invalidates the
    # signature, and an unsigned bundle hangs in glfwInit() on macOS rather
    # than failing loudly. Re-sign on every build so iterating never trips it.
    add_custom_command(TARGET Infinite POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:Infinite>"
        COMMENT "Ad-hoc signing the bundle")

    target_sources(Infinite PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets/Infinite.icns")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/assets/Infinite.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()
