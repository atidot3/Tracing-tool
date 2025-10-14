cmake_minimum_required(VERSION 3.20)
project(trace_viewer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# --- GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.3.8
)
FetchContent_MakeAvailable(glfw)

# --- GLAD
# IMPORTANT: set GLAD options BEFORE FetchContent_MakeAvailable so that the glad CMakeLists
# picks them up and generates the glad source properly.
# We request OpenGL 3.3 Core and we ask glad to generate a loader.
set(GLAD_API "gl=3.3" CACHE STRING "glad API" FORCE)
set(GLAD_PROFILE "core" CACHE STRING "glad profile" FORCE)
set(GLAD_GENERATE_HEADERS ON CACHE BOOL "Generate glad headers" FORCE)
# optionally disable generation of extensions registry if not needed:
#set(GLAD_EXTENSIONS "" CACHE STRING "" FORCE)

FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG        v0.1.36
)
# Make available - this will configure and add a glad target if the subproject provides it.
FetchContent_MakeAvailable(glad)

# Some glad builds create a target named 'glad' or 'glad_static' - prefer 'glad' and fallback.
if (TARGET glad)
  set(GLAD_TARGET glad)
elseif (TARGET glad_static)
  set(GLAD_TARGET glad_static)
else()
  message(STATUS "GLAD target not found after FetchContent; you may need to provide glad.c manually or adjust the glad subproject.")
  set(GLAD_TARGET "")
endif()

# --- Glad include directories (fallback)
# If repository created glad target, it should handle include dirs. If not, you can add them manually later.

# --- Glad alternative: if GLAD_TARGET empty, user can fallback to third_party/glad (handled in instructions below)

# --- Glad loader dependency note: ensure we link it before imgui backends which may use GL symbols.
# We'll link glad into our executable target later conditionally.

# --- Glad and Glad header ordering will be handled in main.cpp (include glad before glfw)

# --- Glad: For safety, if glad produced a C file target we ensure it's available to link

# --- GLAD done

# --- Glad can conflict with some platforms; ensure OpenGL is found
find_package(OpenGL REQUIRED)

# --- GLAD linking will be added later when trace_viewer target is defined

# --- glad is now available (or not) — proceed to other deps

# Glad loader (if not present as FetchContent target) fallback will be handled later.

# --- Glad: nothing more here

# --- Glad: done

# --- GLAD done above; now FetchContent ImGui and nlohmann JSON
FetchContent_Declare(
  imgui
  GIT_REPOSITORY    https://github.com/ocornut/imgui.git
  GIT_TAG           docking
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# --- GLAD: prepare ImGui backend source list (backends are in imgui repo)
file(GLOB IMGUI_BACKENDS
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
add_library(imgui_lib STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw)

# --- Sources
add_executable(trace_viewer
  src/main.cpp
  src/model.hpp
  src/parser.hpp
  src/parser.cpp
  src/color_helper.hpp
  src/utils.hpp
  
  src/ViewerApp.cpp
  src/ViewerApp.hpp
  
  src/ViewportAnim.cpp
  src/ViewportAnim.hpp

  src/ViewerTimeAbsolue.hpp
  src/ViewerTimeAbsolue.cpp

  src/ViewerSelectedPanel.hpp
  src/ViewerSelectedPanel.cpp

  ${IMGUI_BACKENDS}
)

target_include_directories(trace_viewer PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(trace_viewer PRIVATE
  glfw
  imgui_lib
  nlohmann_json::nlohmann_json
)

# If GLAD target was created by the glad subproject, link it. Otherwise, user must add local glad.
if (GLAD_TARGET)
  target_link_libraries(trace_viewer PRIVATE ${GLAD_TARGET})
else()
  message(WARNING "GLAD target not detected. If build fails later with missing glad, follow the fallback instructions in the README to add third_party/glad (glad.c + headers).")
endif()

# platform-specific linking
if (WIN32)
  target_link_libraries(trace_viewer PRIVATE opengl32)
elseif(APPLE)
  find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
  find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
  find_library(CORE_VIDEO_FRAMEWORK CoreVideo REQUIRED)
  target_link_libraries(trace_viewer PRIVATE ${COCOA_FRAMEWORK} ${IOKIT_FRAMEWORK} ${CORE_VIDEO_FRAMEWORK})
  find_package(OpenGL REQUIRED)
  target_link_libraries(trace_viewer PRIVATE OpenGL::GL)
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(trace_viewer PRIVATE OpenGL::GL dl)
endif()

# output dir
set_target_properties(trace_viewer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
