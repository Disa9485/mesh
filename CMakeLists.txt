cmake_minimum_required(VERSION 3.20)

project(mesh_editor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    third_party/imgui
    third_party/imgui/backends
)

target_link_libraries(imgui PUBLIC
    glfw
    glad::glad
    opengl32
)

add_subdirectory(third_party/psd_sdk/src/Psd)

add_executable(mesh_editor
    main.cpp
    delaunator.cpp
    EditorDocument.cpp
    EditorHistory.cpp
    EditorSettings.cpp
    EditorUi.cpp
    EditorUiInteraction.inc
    EditorUiLayers.inc
    EditorUiPanels.inc
    EditorUiParameters.inc
    EditorUiProject.inc
    EditorUiRendering.inc
    EditorUiSelection.inc
    EditorUiShortcuts.inc
    EditorUiTransforms.inc
    EditorUiViewportMath.inc
    EditorUiViewportPicking.inc
    EditorUiViewportWindow.inc
    ImGuiLayer.cpp
    MeshGenerator.cpp
    PsdLoader.cpp
)

target_include_directories(mesh_editor PRIVATE
    .
    third_party/psd_sdk/src/Psd
)

target_link_libraries(mesh_editor PRIVATE
    glfw
    glad::glad
    opengl32
    imgui
    Psd
)

add_custom_command(TARGET mesh_editor POST_BUILD
    COMMAND cmd /c
        "robocopy \"${CMAKE_SOURCE_DIR}\\assets\" \"$<TARGET_FILE_DIR:mesh_editor>\\assets\" /E /XO /R:1 /W:1 /NFL /NDL /NJH /NJS /NP & if %ERRORLEVEL% LSS 8 exit /b 0 else exit /b %ERRORLEVEL%"
)

target_compile_definitions(mesh_editor PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
