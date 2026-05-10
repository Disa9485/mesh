#include "EditorDocument.hpp"
#include "EditorSettings.hpp"
#include "EditorTypes.hpp"
#include "EditorUi.hpp"
#include "ImGuiLayer.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

static void GlfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << "\n";
}

static int RectOverlapArea(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    const int x0 = std::max(ax, bx);
    const int y0 = std::max(ay, by);
    const int x1 = std::min(ax + aw, bx + bw);
    const int y1 = std::min(ay + ah, by + bh);
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }
    return (x1 - x0) * (y1 - y0);
}

static GLFWmonitor* FindMonitorForSettings(const EditorWindowSettings& settings) {
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    if (!monitors || monitorCount <= 0) {
        return glfwGetPrimaryMonitor();
    }

    GLFWmonitor* geometryMatch = nullptr;
    GLFWmonitor* centerMatch = nullptr;
    const int centerX = settings.x + settings.width / 2;
    const int centerY = settings.y + settings.height / 2;

    for (int i = 0; i < monitorCount; ++i) {
        int mx = 0;
        int my = 0;
        int mw = 0;
        int mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);

        const char* name = glfwGetMonitorName(monitors[i]);
        if (settings.hasMonitor && name && settings.monitorName == name &&
            settings.monitorX == mx && settings.monitorY == my &&
            settings.monitorWidth == mw && settings.monitorHeight == mh) {
            return monitors[i];
        }

        if (settings.hasMonitor &&
            settings.monitorX == mx && settings.monitorY == my &&
            settings.monitorWidth == mw && settings.monitorHeight == mh) {
            geometryMatch = monitors[i];
        }

        if (centerX >= mx && centerX < mx + mw && centerY >= my && centerY < my + mh) {
            centerMatch = monitors[i];
        }
    }

    if (geometryMatch) {
        return geometryMatch;
    }
    if (centerMatch) {
        return centerMatch;
    }
    return glfwGetPrimaryMonitor();
}

static bool IsMinimizedSentinelPosition(int x, int y) {
    return x <= -30000 || y <= -30000;
}

static bool RectIntersectsAnyMonitor(int x, int y, int width, int height) {
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    if (!monitors || monitorCount <= 0) {
        return true;
    }

    for (int i = 0; i < monitorCount; ++i) {
        int mx = 0;
        int my = 0;
        int mw = 0;
        int mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        if (RectOverlapArea(x, y, width, height, mx, my, mw, mh) > 0) {
            return true;
        }
    }

    return false;
}

static EditorWindowSettings MakeVisibleWindowSettings(EditorWindowSettings settings) {
    settings.width = std::max(320, settings.width);
    settings.height = std::max(240, settings.height);

    if (
        !settings.hasPlacement ||
        settings.maximized ||
        (!IsMinimizedSentinelPosition(settings.x, settings.y) &&
         RectIntersectsAnyMonitor(settings.x, settings.y, settings.width, settings.height))
    ) {
        return settings;
    }

    GLFWmonitor* monitor = FindMonitorForSettings(settings);
    int mx = 80;
    int my = 80;
    int mw = 1280;
    int mh = 800;
    if (monitor) {
        glfwGetMonitorWorkarea(monitor, &mx, &my, &mw, &mh);
    }

    settings.width = std::min(settings.width, std::max(320, mw));
    settings.height = std::min(settings.height, std::max(240, mh));
    settings.x = mx + std::max(0, (mw - settings.width) / 2);
    settings.y = my + std::max(0, (mh - settings.height) / 2);
    settings.hasPlacement = true;
    return settings;
}

static GLFWmonitor* FindMonitorForWindow(GLFWwindow* window) {
    int wx = 0;
    int wy = 0;
    int ww = 0;
    int wh = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);

    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    if (!monitors || monitorCount <= 0) {
        return glfwGetPrimaryMonitor();
    }

    GLFWmonitor* best = glfwGetPrimaryMonitor();
    int bestOverlap = -1;
    for (int i = 0; i < monitorCount; ++i) {
        int mx = 0;
        int my = 0;
        int mw = 0;
        int mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        const int overlap = RectOverlapArea(wx, wy, ww, wh, mx, my, mw, mh);
        if (overlap > bestOverlap) {
            best = monitors[i];
            bestOverlap = overlap;
        }
    }
    return best;
}

static void SaveMonitorPlacement(GLFWmonitor* monitor, EditorWindowSettings& settings) {
    if (!monitor) {
        settings.hasMonitor = false;
        settings.monitorName.clear();
        return;
    }

    int mx = 0;
    int my = 0;
    int mw = 0;
    int mh = 0;
    glfwGetMonitorWorkarea(monitor, &mx, &my, &mw, &mh);

    const char* name = glfwGetMonitorName(monitor);
    settings.monitorName = name ? name : "";
    settings.monitorX = mx;
    settings.monitorY = my;
    settings.monitorWidth = mw;
    settings.monitorHeight = mh;
    settings.hasMonitor = true;
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

    const EditorWindowSettings windowSettings = MakeVisibleWindowSettings(LoadEditorWindowSettings());

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

    if (windowSettings.hasPlacement) {
        if (windowSettings.maximized) {
            GLFWmonitor* monitor = FindMonitorForSettings(windowSettings);
            int mx = windowSettings.x;
            int my = windowSettings.y;
            int mw = windowSettings.width;
            int mh = windowSettings.height;
            if (monitor) {
                glfwGetMonitorWorkarea(monitor, &mx, &my, &mw, &mh);
            }
            glfwSetWindowPos(window, mx, my);
            glfwSetWindowSize(window, std::max(320, mw), std::max(240, mh));
            glfwMaximizeWindow(window);
        } else {
            glfwSetWindowPos(window, windowSettings.x, windowSettings.y);
        }
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

    std::string startupPath;
    if (argc >= 2 && argv[1]) {
        startupPath = argv[1];
    } else {
        startupPath = LoadLastProjectPath();
        if (startupPath.empty()) {
            startupPath = LoadLastPsdPath();
        }
    }

    if (startupPath.empty()) {
        startupPath = (std::filesystem::path("assets") / "new_project.mesh.bin").string();
        editor.loadingOperation = EditorLoadingOperation::NewProject;
    } else {
        editor.loadingOperation = EditorLoadingOperation::LoadPath;
    }
    editor.pendingPath = startupPath;
    editor.loadingPath = startupPath;
    editor.loadingDisplayName = std::filesystem::path(startupPath).filename().string();
    if (editor.loadingDisplayName.size() > 9u && editor.loadingDisplayName.substr(editor.loadingDisplayName.size() - 9u) == ".mesh.bin") {
        editor.loadingDisplayName.resize(editor.loadingDisplayName.size() - 9u);
    } else {
        editor.loadingDisplayName = std::filesystem::path(editor.loadingDisplayName).stem().string();
    }
    if (editor.loadingDisplayName.empty()) {
        editor.loadingDisplayName = "new_project";
    }
    editor.loadingScreenActive = true;
    editor.loadingFramesBeforeExecute = 1;
    editor.statusText = "Loading " + editor.loadingDisplayName + ".";

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

    const bool windowIconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED) == GLFW_TRUE;
    if (!windowIconified) {
        EditorWindowSettings savedWindowSettings;
        savedWindowSettings.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
        glfwGetWindowPos(window, &savedWindowSettings.x, &savedWindowSettings.y);
        glfwGetWindowSize(window, &savedWindowSettings.width, &savedWindowSettings.height);
        savedWindowSettings.hasPlacement = true;
        if (!IsMinimizedSentinelPosition(savedWindowSettings.x, savedWindowSettings.y)) {
            SaveMonitorPlacement(FindMonitorForWindow(window), savedWindowSettings);
            SaveEditorWindowSettings(savedWindowSettings);
        }
    }

    if (!editor.document.projectPath.empty()) {
        SaveLastProjectPath(editor.document.projectPath);
    }

    DestroyDocument(editor.document);
    imgui.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}
