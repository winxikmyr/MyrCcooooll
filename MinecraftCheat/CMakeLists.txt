cmake_minimum_required(VERSION 3.20)
project(MyrLegit VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ── Статическая линковка VC++ Runtime (не нужен VC++ Redist на целевой машине) ─
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ── Output в БИЛДЫ/ ───────────────────────────────────────────────────────────
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../БИЛДЫ")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}/lib")
foreach(CFG Release Debug RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CFG} CFG_U)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_U} "${OUTPUT_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_U} "${OUTPUT_DIR}")
endforeach()

# ─── MinHook (локально в libs/minhook) ───────────────────────────────────────
set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/libs/minhook")
add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)
set_target_properties(minhook PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# ─── Dear ImGui (локально в libs/imgui) ──────────────────────────────────────
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/libs/imgui")
add_library(imgui_lib STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC opengl32)
set_target_properties(imgui_lib PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# ─── JNI (jni.h из JDK) ───────────────────────────────────────────────────────
find_package(JNI QUIET)
if(JNI_FOUND)
    message(STATUS "[VasyCheat] JNI found via CMake: ${JNI_INCLUDE_DIRS}")
    set(JNI_DIRS ${JNI_INCLUDE_DIRS})
elseif(DEFINED ENV{JAVA_HOME})
    set(JNI_DIRS "$ENV{JAVA_HOME}/include" "$ENV{JAVA_HOME}/include/win32")
    message(STATUS "[VasyCheat] JNI via JAVA_HOME: $ENV{JAVA_HOME}")
else()
    foreach(JDK_PATH
            "C:/Program Files/Eclipse Adoptium/jdk-8.0.504.1-hotspot"
            "C:/Program Files/Eclipse Adoptium/jdk-8.0.392.8-hotspot"
            "C:/Program Files/Java/jdk1.8.0_361"
            "C:/Program Files/Java/jdk1.8.0_333")
        if(EXISTS "${JDK_PATH}/include/jni.h")
            set(JNI_DIRS "${JDK_PATH}/include" "${JDK_PATH}/include/win32")
            message(STATUS "[VasyCheat] JNI at: ${JDK_PATH}")
            break()
        endif()
    endforeach()
    if(NOT JNI_DIRS)
        message(WARNING "[VasyCheat] jni.h not found! Set JAVA_HOME.")
    endif()
endif()

# ─── Cheat DLL ────────────────────────────────────────────────────────────────
add_library(MyrLegit SHARED
    src/dllmain.cpp
    src/hooks/gl_hook.cpp
    src/hooks/wnd_hook.cpp
    src/jvm/jvm_bridge.cpp
    src/gui/gui_manager.cpp
    src/gui/clickgui.cpp
    src/gui/hud.cpp
    src/gui/hud_plus.cpp
    src/gui/nametags.cpp
    src/gui/config_manager.cpp
    src/overlay/stream_overlay.cpp
)

target_include_directories(MyrLegit PRIVATE src/ ${JNI_DIRS})

target_link_libraries(MyrLegit PRIVATE
    imgui_lib
    minhook
    opengl32
    gdi32
    dwmapi
    d3d9
)

target_compile_definitions(MyrLegit PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

set_target_properties(MyrLegit PROPERTIES
    OUTPUT_NAME "MyrLegit"
    PREFIX      ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# ─── Injector ─────────────────────────────────────────────────────────────────
add_subdirectory(injector)
