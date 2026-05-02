#include "EditorDocument.hpp"
#include "EditorSettings.hpp"
#include "EditorTypes.hpp"
#include "EditorUi.hpp"
#include "ImGuiLayer.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

static void GlfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << "\n";
}

int main(int argc, char** argv) {
    glfwSetErrorCallback(GlfwErrorCallback);

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW.\n";
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    const EditorWindowSettings windowSettings = LoadEditorWindowSettings();
    if (windowSettings.maximized) {
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    }

    GLFWwindow* window = glfwCreateWindow(
        windowSettings.width,
        windowSettings.height,
        "PSD Mesh Editor",
        nullptr,
        nullptr
    );

    if (!window) {
        std::cerr << "Failed to create GLFW window.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    if (windowSettings.hasPlacement && !windowSettings.maximized) {
        glfwSetWindowPos(window, windowSettings.x, windowSettings.y);
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "Failed to load GLAD.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ImGuiLayer imgui;
    if (!imgui.initialize(window)) {
        std::cerr << "Failed to initialize ImGui.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    EditorState editor;

    if (argc >= 2 && argv[1]) {
        editor.pendingPath = argv[1];
    } else {
        editor.pendingPath = LoadLastProjectPath();
        if (editor.pendingPath.empty()) {
            editor.pendingPath = LoadLastPsdPath();
        }
    }

    if (!editor.pendingPath.empty()) {
        std::string error;
        const std::filesystem::path pending(editor.pendingPath);
        const bool loadAsPsd = pending.extension() == ".psd" || pending.extension() == ".PSD";
        const bool loaded = loadAsPsd
            ? LoadPsdIntoEditor(editor.pendingPath, editor, window, error)
            : LoadProjectForEditor(editor.pendingPath, editor, window, error);
        if (!loaded) {
            editor.errorText = error;
            editor.statusText = "Failed to auto-load project.";
        }
    }

    glfwSetWindowUserPointer(window, &editor);
    glfwSetDropCallback(window, DropCallback);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int fbw = 0;
        int fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);

        glViewport(0, 0, fbw, fbh);
        glClearColor(0.08f, 0.085f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        imgui.beginFrame();
        DrawEditor(editor, window);
        imgui.endFrame();

        glfwSwapBuffers(window);
    }

    EditorWindowSettings savedWindowSettings;
    savedWindowSettings.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
    glfwGetWindowPos(window, &savedWindowSettings.x, &savedWindowSettings.y);
    glfwGetWindowSize(window, &savedWindowSettings.width, &savedWindowSettings.height);
    savedWindowSettings.hasPlacement = true;
    SaveEditorWindowSettings(savedWindowSettings);

    if (!editor.document.projectPath.empty()) {
        SaveLastProjectPath(editor.document.projectPath);
    }

    DestroyDocument(editor.document);
    imgui.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}
