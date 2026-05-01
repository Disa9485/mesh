#pragma once

struct GLFWwindow;

class ImGuiLayer {
public:
    bool initialize(GLFWwindow* window, const char* glsl_version = "#version 330");
    void shutdown();

    void beginFrame();
    void endFrame();

private:
    GLFWwindow* window_ = nullptr;
    bool initialized_ = false;
};