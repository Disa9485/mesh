static float LayerEffectiveRenderOrder(const EditorLayer& layer, int layerIndex) {
    if (!layer.renderOrderOverride.empty()) {
        char* end = nullptr;
        const float value = std::strtof(layer.renderOrderOverride.c_str(), &end);
        if (end && end != layer.renderOrderOverride.c_str()) {
            return value;
        }
    }

    return static_cast<float>(layerIndex);
}

struct MaskRenderGlState {
    GLint currentProgram = 0;
    GLint activeTexture = 0;
    GLint textureBinding = 0;
    GLint arrayBuffer = 0;
    GLint elementArrayBuffer = 0;
    GLint vertexArray = 0;
    GLint blendSrcRgb = 0;
    GLint blendDstRgb = 0;
    GLint blendSrcAlpha = 0;
    GLint blendDstAlpha = 0;
    GLint blendEquationRgb = 0;
    GLint blendEquationAlpha = 0;
    GLboolean blend = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean depthTest = GL_FALSE;
    GLboolean scissorTest = GL_FALSE;
    GLboolean stencilTest = GL_FALSE;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint stencilFunc = GL_ALWAYS;
    GLint stencilRef = 0;
    GLint stencilValueMask = 0xFF;
    GLint stencilWriteMask = 0xFF;
    GLint stencilFail = GL_KEEP;
    GLint stencilPassDepthFail = GL_KEEP;
    GLint stencilPassDepthPass = GL_KEEP;
};

struct MaskRenderResources {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLint displaySizeLocation = -1;
};

struct LayerColorRenderResources {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLint displaySizeLocation = -1;
    GLint projectionLocation = -1;
    GLint hueShiftLocation = -1;
    GLint saturationScaleLocation = -1;
    GLint lightnessShiftLocation = -1;
    GLint opacityLocation = -1;
};

struct LayerColorDrawParams {
    int layerIndex = -1;
    ImVec2 imagePos = ImVec2(0.0f, 0.0f);
    ImVec2 imageSize = ImVec2(0.0f, 0.0f);
    float zoom = 1.0f;
    bool deformMode = false;
    float hueShift = 0.0f;
    float saturationScale = 100.0f;
    float lightnessShift = 0.0f;
    float opacity = 1.0f;
};

static EditorState* gMaskRenderEditor = nullptr;
static ImVec2 gMaskRenderImagePos;
static float gMaskRenderZoom = 1.0f;
static MaskRenderGlState gMaskRenderSavedState;
static std::vector<LayerColorDrawParams> gLayerColorDrawParams;

static GLuint CompileMaskShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static MaskRenderResources& GetMaskRenderResources() {
    static MaskRenderResources resources;
    if (resources.program) {
        return resources;
    }

    constexpr const char* vertexSource = R"GLSL(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        uniform vec2 uDisplaySize;
        out vec2 vUv;
        void main() {
            vec2 ndc = vec2(
                (aPos.x / uDisplaySize.x) * 2.0 - 1.0,
                1.0 - (aPos.y / uDisplaySize.y) * 2.0
            );
            gl_Position = vec4(ndc, 0.0, 1.0);
            vUv = aUv;
        }
    )GLSL";

    constexpr const char* fragmentSource = R"GLSL(
        #version 330 core
        uniform sampler2D uTexture;
        in vec2 vUv;
        out vec4 FragColor;
        void main() {
            float alpha = texture(uTexture, vUv).a;
            if (alpha <= 0.0039) {
                discard;
            }
            FragColor = vec4(1.0);
        }
    )GLSL";

    const GLuint vertexShader = CompileMaskShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = CompileMaskShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertexShader || !fragmentShader) {
        if (vertexShader) {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader) {
            glDeleteShader(fragmentShader);
        }
        return resources;
    }

    resources.program = glCreateProgram();
    glAttachShader(resources.program, vertexShader);
    glAttachShader(resources.program, fragmentShader);
    glLinkProgram(resources.program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = 0;
    glGetProgramiv(resources.program, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(resources.program);
        resources.program = 0;
        return resources;
    }

    resources.displaySizeLocation = glGetUniformLocation(resources.program, "uDisplaySize");
    glUseProgram(resources.program);
    glUniform1i(glGetUniformLocation(resources.program, "uTexture"), 0);

    glGenVertexArrays(1, &resources.vao);
    glGenBuffers(1, &resources.vbo);
    glGenBuffers(1, &resources.ebo);

    glBindVertexArray(resources.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources.ebo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glBindVertexArray(0);

    return resources;
}

static LayerColorRenderResources& GetLayerColorRenderResources() {
    static LayerColorRenderResources resources;
    if (resources.program) {
        return resources;
    }

    constexpr const char* vertexSource = R"GLSL(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        uniform vec2 uDisplaySize;
        out vec2 vUv;
        void main() {
            vec2 ndc = vec2(
                (aPos.x / uDisplaySize.x) * 2.0 - 1.0,
                1.0 - (aPos.y / uDisplaySize.y) * 2.0
            );
            gl_Position = vec4(ndc, 0.0, 1.0);
            vUv = aUv;
        }
    )GLSL";

    constexpr const char* fragmentSource = R"GLSL(
        #version 330 core
        uniform sampler2D Texture;
        uniform float uHueShift;
        uniform float uSaturationScale;
        uniform float uLightnessShift;
        uniform float uOpacity;
        in vec2 vUv;
        layout (location = 0) out vec4 Out_Color;

        vec3 rgbToHsl(vec3 c) {
            float maxc = max(max(c.r, c.g), c.b);
            float minc = min(min(c.r, c.g), c.b);
            float h = 0.0;
            float s = 0.0;
            float l = (maxc + minc) * 0.5;
            float d = maxc - minc;
            if (d > 0.00001) {
                s = l > 0.5 ? d / (2.0 - maxc - minc) : d / (maxc + minc);
                if (maxc == c.r) {
                    h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
                } else if (maxc == c.g) {
                    h = (c.b - c.r) / d + 2.0;
                } else {
                    h = (c.r - c.g) / d + 4.0;
                }
                h /= 6.0;
            }
            return vec3(h, s, l);
        }

        float hueToRgb(float p, float q, float t) {
            if (t < 0.0) t += 1.0;
            if (t > 1.0) t -= 1.0;
            if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
            if (t < 1.0 / 2.0) return q;
            if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
            return p;
        }

        vec3 hslToRgb(vec3 hsl) {
            float h = hsl.x;
            float s = hsl.y;
            float l = hsl.z;
            if (s <= 0.00001) {
                return vec3(l);
            }
            float q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
            float p = 2.0 * l - q;
            return vec3(
                hueToRgb(p, q, h + 1.0 / 3.0),
                hueToRgb(p, q, h),
                hueToRgb(p, q, h - 1.0 / 3.0)
            );
        }

        void main() {
            vec4 tex = texture(Texture, vUv);
            float alpha = tex.a * uOpacity;
            if (alpha <= 0.0) {
                Out_Color = vec4(0.0);
                return;
            }

            vec3 straight = tex.a > 0.00001 ? tex.rgb / tex.a : vec3(0.0);
            vec3 hsl = rgbToHsl(clamp(straight, 0.0, 1.0));
            hsl.x = fract(hsl.x + uHueShift);
            hsl.y = clamp(hsl.y * uSaturationScale, 0.0, 1.0);
            hsl.z = clamp(hsl.z + uLightnessShift, 0.0, 1.0);
            vec3 rgb = hslToRgb(hsl);
            Out_Color = vec4(rgb * alpha, alpha);
        }
    )GLSL";

    const GLuint vertexShader = CompileMaskShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = CompileMaskShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertexShader || !fragmentShader) {
        if (vertexShader) {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader) {
            glDeleteShader(fragmentShader);
        }
        return resources;
    }

    resources.program = glCreateProgram();
    glAttachShader(resources.program, vertexShader);
    glAttachShader(resources.program, fragmentShader);
    glLinkProgram(resources.program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = 0;
    glGetProgramiv(resources.program, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(resources.program);
        resources.program = 0;
        return resources;
    }

    resources.displaySizeLocation = glGetUniformLocation(resources.program, "uDisplaySize");
    resources.hueShiftLocation = glGetUniformLocation(resources.program, "uHueShift");
    resources.saturationScaleLocation = glGetUniformLocation(resources.program, "uSaturationScale");
    resources.lightnessShiftLocation = glGetUniformLocation(resources.program, "uLightnessShift");
    resources.opacityLocation = glGetUniformLocation(resources.program, "uOpacity");
    glUseProgram(resources.program);
    glUniform1i(glGetUniformLocation(resources.program, "Texture"), 0);

    glGenVertexArrays(1, &resources.vao);
    glGenBuffers(1, &resources.vbo);
    glGenBuffers(1, &resources.ebo);

    glBindVertexArray(resources.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources.ebo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glBindVertexArray(0);
    return resources;
}

static bool LayerHasColorAdjustment(const EditorLayer& layer) {
    return
        std::abs(layer.hueShift) > 0.001f ||
        std::abs(layer.saturationScale - 100.0f) > 0.001f ||
        std::abs(layer.lightnessShift) > 0.001f;
}

static void SaveMaskRenderGlState(MaskRenderGlState& state);
static void RestoreMaskRenderGlState(const MaskRenderGlState& state);

static void DrawLayerColorAdjustCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    const LayerColorDrawParams* params = static_cast<const LayerColorDrawParams*>(cmd->UserCallbackData);
    if (!params || !gMaskRenderEditor || !IsValidLayerIndex(*gMaskRenderEditor, params->layerIndex)) {
        return;
    }

    const EditorLayer& layer = gMaskRenderEditor->document.layers[params->layerIndex];
    if (!layer.texture) {
        return;
    }

    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;

    if (params->deformMode) {
        if (layer.mesh.vertices.empty() || layer.mesh.indices.empty()) {
            return;
        }

        vertices.reserve(layer.mesh.vertices.size() * 4u);
        for (const MeshVertex& vertex : layer.mesh.vertices) {
            const ImVec2 screen = CanvasToScreenPoint(vertex.position, params->imagePos, params->zoom);
            vertices.push_back(screen.x);
            vertices.push_back(screen.y);
            vertices.push_back(vertex.uv.x);
            vertices.push_back(vertex.uv.y);
        }

        indices.reserve(layer.mesh.indices.size());
        for (std::size_t i = 0; i + 2u < layer.mesh.indices.size(); i += 3u) {
            const std::uint32_t ia = layer.mesh.indices[i + 0u];
            const std::uint32_t ib = layer.mesh.indices[i + 1u];
            const std::uint32_t ic = layer.mesh.indices[i + 2u];
            if (
                ia >= layer.mesh.vertices.size() ||
                ib >= layer.mesh.vertices.size() ||
                ic >= layer.mesh.vertices.size() ||
                !MeshTriangleEdgesValid(layer.mesh, ia, ib, ic)
            ) {
                continue;
            }

            indices.push_back(ia);
            indices.push_back(ib);
            indices.push_back(ic);
        }
    } else {
        const ImVec2 min = params->imagePos;
        const ImVec2 max(params->imagePos.x + params->imageSize.x, params->imagePos.y + params->imageSize.y);
        vertices = {
            min.x, min.y, 0.0f, 0.0f,
            max.x, min.y, 1.0f, 0.0f,
            max.x, max.y, 1.0f, 1.0f,
            min.x, max.y, 0.0f, 1.0f
        };
        indices = {0u, 1u, 2u, 0u, 2u, 3u};
    }

    if (indices.empty()) {
        return;
    }

    LayerColorRenderResources& resources = GetLayerColorRenderResources();
    if (!resources.program) {
        return;
    }

    MaskRenderGlState savedState;
    SaveMaskRenderGlState(savedState);

    const ImGuiIO& io = ImGui::GetIO();
    glUseProgram(resources.program);
    glUniform2f(resources.displaySizeLocation, io.DisplaySize.x, io.DisplaySize.y);
    glUniform1f(resources.hueShiftLocation, std::clamp(params->hueShift, -100.0f, 100.0f) / 100.0f);
    glUniform1f(resources.saturationScaleLocation, std::clamp(params->saturationScale, 0.0f, 200.0f) / 100.0f);
    glUniform1f(resources.lightnessShiftLocation, std::clamp(params->lightnessShift, -100.0f, 100.0f) / 100.0f);
    glUniform1f(resources.opacityLocation, std::clamp(params->opacity, 0.0f, 1.0f));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, layer.texture);
    glBindVertexArray(resources.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);

    RestoreMaskRenderGlState(savedState);
}

static void SaveMaskRenderGlState(MaskRenderGlState& state) {
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.currentProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.textureBinding);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &state.elementArrayBuffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vertexArray);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state.blendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.blendEquationAlpha);
    state.blend = glIsEnabled(GL_BLEND);
    state.cullFace = glIsEnabled(GL_CULL_FACE);
    state.depthTest = glIsEnabled(GL_DEPTH_TEST);
    state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
    state.stencilTest = glIsEnabled(GL_STENCIL_TEST);
    glGetBooleanv(GL_COLOR_WRITEMASK, state.colorMask);
    glGetIntegerv(GL_STENCIL_FUNC, &state.stencilFunc);
    glGetIntegerv(GL_STENCIL_REF, &state.stencilRef);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &state.stencilValueMask);
    glGetIntegerv(GL_STENCIL_WRITEMASK, &state.stencilWriteMask);
    glGetIntegerv(GL_STENCIL_FAIL, &state.stencilFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &state.stencilPassDepthFail);
    glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &state.stencilPassDepthPass);
    glActiveTexture(state.activeTexture);
}

static void RestoreMaskRenderGlState(const MaskRenderGlState& state) {
    glUseProgram(static_cast<GLuint>(state.currentProgram));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.textureBinding));
    glBindVertexArray(static_cast<GLuint>(state.vertexArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.arrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(state.elementArrayBuffer));
    glBlendEquationSeparate(state.blendEquationRgb, state.blendEquationAlpha);
    glBlendFuncSeparate(state.blendSrcRgb, state.blendDstRgb, state.blendSrcAlpha, state.blendDstAlpha);
    state.blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    state.cullFace ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    state.depthTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    state.scissorTest ? glEnable(GL_SCISSOR_TEST) : glDisable(GL_SCISSOR_TEST);
    state.stencilTest ? glEnable(GL_STENCIL_TEST) : glDisable(GL_STENCIL_TEST);
    glColorMask(state.colorMask[0], state.colorMask[1], state.colorMask[2], state.colorMask[3]);
    glStencilFunc(state.stencilFunc, state.stencilRef, state.stencilValueMask);
    glStencilMask(state.stencilWriteMask);
    glStencilOp(state.stencilFail, state.stencilPassDepthFail, state.stencilPassDepthPass);
    glActiveTexture(state.activeTexture);
}

static void DrawMaskLayerToStencil(const EditorLayer& layer) {
    if (
        !layer.visible ||
        !layer.texture ||
        layer.mesh.vertices.empty() ||
        layer.mesh.indices.empty()
    ) {
        return;
    }

    MaskRenderResources& resources = GetMaskRenderResources();
    if (!resources.program) {
        return;
    }

    std::vector<float> vertices;
    vertices.reserve(layer.mesh.vertices.size() * 4u);
    for (const MeshVertex& vertex : layer.mesh.vertices) {
        const ImVec2 screen = CanvasToScreenPoint(vertex.position, gMaskRenderImagePos, gMaskRenderZoom);
        vertices.push_back(screen.x);
        vertices.push_back(screen.y);
        vertices.push_back(vertex.uv.x);
        vertices.push_back(vertex.uv.y);
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(layer.mesh.indices.size());
    for (std::size_t i = 0; i + 2u < layer.mesh.indices.size(); i += 3u) {
        const std::uint32_t ia = layer.mesh.indices[i + 0u];
        const std::uint32_t ib = layer.mesh.indices[i + 1u];
        const std::uint32_t ic = layer.mesh.indices[i + 2u];
        if (
            ia >= layer.mesh.vertices.size() ||
            ib >= layer.mesh.vertices.size() ||
            ic >= layer.mesh.vertices.size() ||
            !MeshTriangleEdgesValid(layer.mesh, ia, ib, ic)
        ) {
            continue;
        }

        indices.push_back(ia);
        indices.push_back(ib);
        indices.push_back(ic);
    }

    if (indices.empty()) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 displaySize = io.DisplaySize;

    glUseProgram(resources.program);
    glUniform2f(resources.displaySizeLocation, displaySize.x, displaySize.y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, layer.texture);
    glBindVertexArray(resources.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
}

static void BeginMaskedLayerStencilCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    if (!gMaskRenderEditor) {
        return;
    }

    const int layerIndex = static_cast<int>(reinterpret_cast<intptr_t>(cmd->UserCallbackData));
    if (!IsValidLayerIndex(*gMaskRenderEditor, layerIndex)) {
        return;
    }

    const EditorLayer& layer = gMaskRenderEditor->document.layers[layerIndex];
    SaveMaskRenderGlState(gMaskRenderSavedState);

    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    for (const int maskLayerIndex : layer.maskLayerIndices) {
        if (
            !IsValidLayerIndex(*gMaskRenderEditor, maskLayerIndex) ||
            maskLayerIndex == layerIndex
        ) {
            continue;
        }

        DrawMaskLayerToStencil(gMaskRenderEditor->document.layers[maskLayerIndex]);
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    RestoreMaskRenderGlState(gMaskRenderSavedState);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

static void EndMaskedLayerStencilCallback(const ImDrawList*, const ImDrawCmd*) {
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
}

static void DrawLayerTexture(
    ImDrawList* drawList,
    const EditorLayer& layer,
    ImVec2 imagePos,
    ImVec2 imageSize,
    ImU32 tint
) {
    if (!layer.texture) {
        return;
    }

    drawList->AddImage(
        static_cast<ImTextureID>(static_cast<intptr_t>(layer.texture)),
        imagePos,
        ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y),
        ImVec2(0.0f, 0.0f),
        ImVec2(1.0f, 1.0f),
        tint
    );
}

static void DrawTexturedLayerMesh(
    ImDrawList* drawList,
    const EditorLayer& layer,
    ImVec2 imagePos,
    float zoom,
    ImU32 tint
) {
    if (
        !layer.texture ||
        layer.mesh.vertices.empty() ||
        layer.mesh.indices.empty()
    ) {
        return;
    }

    const ImTextureID textureId = static_cast<ImTextureID>(
        static_cast<intptr_t>(layer.texture)
    );

    constexpr int kMaxBatchVertices = 60000;
    constexpr int kMaxBatchIndices = kMaxBatchVertices;

    std::vector<std::uint32_t> batch;
    batch.reserve(kMaxBatchIndices);

    auto flushBatch = [&]() {
        if (batch.empty()) {
            return;
        }

        drawList->PrimReserve(
            static_cast<int>(batch.size()),
            static_cast<int>(batch.size())
        );

        const ImDrawIdx vertexOffset = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
        for (int i = 0; i < static_cast<int>(batch.size()); ++i) {
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(vertexOffset + i));
        }

        for (const std::uint32_t index : batch) {
            const MeshVertex& vertex = layer.mesh.vertices[index];
            drawList->PrimWriteVtx(
                CanvasToScreenPoint(vertex.position, imagePos, zoom),
                ImVec2(vertex.uv.x, vertex.uv.y),
                tint
            );
        }

        batch.clear();
    };

    drawList->PushTexture(ImTextureRef(textureId));

    for (std::size_t i = 0; i + 2 < layer.mesh.indices.size(); i += 3) {
        const std::uint32_t ia = layer.mesh.indices[i + 0u];
        const std::uint32_t ib = layer.mesh.indices[i + 1u];
        const std::uint32_t ic = layer.mesh.indices[i + 2u];

        if (
            ia >= layer.mesh.vertices.size() ||
            ib >= layer.mesh.vertices.size() ||
            ic >= layer.mesh.vertices.size() ||
            !MeshTriangleEdgesValid(layer.mesh, ia, ib, ic)
        ) {
            continue;
        }

        if (batch.size() + 3u > static_cast<std::size_t>(kMaxBatchIndices)) {
            flushBatch();
        }

        batch.push_back(ia);
        batch.push_back(ib);
        batch.push_back(ic);
    }

    flushBatch();
    drawList->PopTexture();
}

static void DrawLayerWithGpuMasks(
    ImDrawList* drawList,
    const EditorLayer& layer,
    int layerIndex,
    ImVec2 imagePos,
    ImVec2 imageSize,
    float zoom,
    bool deformMode
) {
    const bool hasMasks = !layer.maskLayerIndices.empty();
    const bool hasColorAdjustment = LayerHasColorAdjustment(layer);
    const int opacityByte = static_cast<int>(std::round(std::clamp(layer.opacity, 0.0f, 1.0f) * 255.0f));
    const ImU32 tint = IM_COL32(opacityByte, opacityByte, opacityByte, opacityByte);
    if (hasMasks) {
        drawList->AddCallback(
            BeginMaskedLayerStencilCallback,
            reinterpret_cast<void*>(static_cast<intptr_t>(layerIndex))
        );
    }

    if (hasColorAdjustment) {
        gLayerColorDrawParams.push_back(LayerColorDrawParams{
            layerIndex,
            imagePos,
            imageSize,
            zoom,
            deformMode,
            layer.hueShift,
            layer.saturationScale,
            layer.lightnessShift,
            layer.opacity
        });
        drawList->AddCallback(DrawLayerColorAdjustCallback, &gLayerColorDrawParams.back());
        drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        drawList->AddCallback(SetPremultipliedAlphaBlendCallback, nullptr);
    } else if (deformMode) {
        DrawTexturedLayerMesh(drawList, layer, imagePos, zoom, tint);
    } else {
        DrawLayerTexture(drawList, layer, imagePos, imageSize, tint);
    }

    if (hasMasks) {
        drawList->AddCallback(EndMaskedLayerStencilCallback, nullptr);
    }
}

static void DrawLayerMeshWireframe(
    ImDrawList* drawList,
    const EditorLayer& layer,
    ImVec2 imagePos,
    float zoom,
    ImU32 color,
    float thickness
) {
    if (!layer.mesh.edges.empty()) {
        for (const MeshEdge& edge : layer.mesh.edges) {
            if (
                edge.a >= layer.mesh.vertices.size() ||
                edge.b >= layer.mesh.vertices.size()
            ) {
                continue;
            }

            const MeshVertex& a = layer.mesh.vertices[edge.a];
            const MeshVertex& b = layer.mesh.vertices[edge.b];
            const ImVec2 pa(imagePos.x + a.position.x * zoom, imagePos.y + a.position.y * zoom);
            const ImVec2 pb(imagePos.x + b.position.x * zoom, imagePos.y + b.position.y * zoom);
            drawList->AddLine(pa, pb, color, thickness);
        }
        return;
    }

    for (std::size_t i = 0; i + 2 < layer.mesh.indices.size(); i += 3) {
        const std::uint32_t ia = layer.mesh.indices[i + 0];
        const std::uint32_t ib = layer.mesh.indices[i + 1];
        const std::uint32_t ic = layer.mesh.indices[i + 2];

        if (
            ia >= layer.mesh.vertices.size() ||
            ib >= layer.mesh.vertices.size() ||
            ic >= layer.mesh.vertices.size()
        ) {
            continue;
        }

        const MeshVertex& a = layer.mesh.vertices[ia];
        const MeshVertex& b = layer.mesh.vertices[ib];
        const MeshVertex& c = layer.mesh.vertices[ic];

        const ImVec2 pa(imagePos.x + a.position.x * zoom, imagePos.y + a.position.y * zoom);
        const ImVec2 pb(imagePos.x + b.position.x * zoom, imagePos.y + b.position.y * zoom);
        const ImVec2 pc(imagePos.x + c.position.x * zoom, imagePos.y + c.position.y * zoom);

        drawList->AddLine(pa, pb, color, thickness);
        drawList->AddLine(pb, pc, color, thickness);
        drawList->AddLine(pc, pa, color, thickness);
    }
}

static void DrawLayerTransformBox(
    ImDrawList* drawList,
    const EditorLayer& layer,
    ImVec2 imagePos,
    float zoom
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetMeshBounds(layer.mesh, x0, y0, x1, y1)) {
        return;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    const ImVec2 p0 = CanvasToScreenPoint(Vec2{x0, y0}, imagePos, zoom);
    const ImVec2 p1 = CanvasToScreenPoint(Vec2{x1, y1}, imagePos, zoom);
    const ImU32 boxColor = IM_COL32(255, 220, 90, 230);
    const ImU32 handleFill = IM_COL32(25, 26, 30, 230);
    const ImU32 moveFill = IM_COL32(8, 9, 12, 255);
    const float handle = 5.0f;

    drawList->AddRect(p0, p1, boxColor, 0.0f, 0, 1.5f);

    const ImVec2 edgeHandles[] = {
        ImVec2((p0.x + p1.x) * 0.5f, p0.y),
        ImVec2(p1.x, (p0.y + p1.y) * 0.5f),
        ImVec2((p0.x + p1.x) * 0.5f, p1.y),
        ImVec2(p0.x, (p0.y + p1.y) * 0.5f),
    };

    for (const ImVec2 h : edgeHandles) {
        drawList->AddRectFilled(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            handleFill
        );
        drawList->AddRect(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            boxColor
        );
    }

    const ImVec2 corners[] = {
        p0,
        ImVec2(p1.x, p0.y),
        p1,
        ImVec2(p0.x, p1.y),
    };

    for (const ImVec2 corner : corners) {
        drawList->AddCircle(corner, 12.0f, IM_COL32(255, 220, 90, 130), 16, 1.0f);
    }

    const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    drawList->AddCircleFilled(center, 8.0f, moveFill, 16);
    drawList->AddCircle(center, 8.0f, boxColor, 16, 1.5f);
}

static void DrawSelectedLayersTransformBox(
    ImDrawList* drawList,
    const EditorState& editor,
    ImVec2 imagePos,
    float zoom
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetSelectedLayerBounds(editor, x0, y0, x1, y1)) {
        return;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    const ImVec2 p0 = CanvasToScreenPoint(Vec2{x0, y0}, imagePos, zoom);
    const ImVec2 p1 = CanvasToScreenPoint(Vec2{x1, y1}, imagePos, zoom);
    const ImU32 boxColor = IM_COL32(255, 220, 90, 230);
    const ImU32 handleFill = IM_COL32(25, 26, 30, 230);
    const ImU32 moveFill = IM_COL32(8, 9, 12, 255);
    const float handle = 5.0f;

    drawList->AddRect(p0, p1, boxColor, 0.0f, 0, 1.5f);

    const ImVec2 edgeHandles[] = {
        ImVec2((p0.x + p1.x) * 0.5f, p0.y),
        ImVec2(p1.x, (p0.y + p1.y) * 0.5f),
        ImVec2((p0.x + p1.x) * 0.5f, p1.y),
        ImVec2(p0.x, (p0.y + p1.y) * 0.5f),
    };

    for (const ImVec2 h : edgeHandles) {
        drawList->AddRectFilled(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            handleFill
        );
        drawList->AddRect(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            boxColor
        );
    }

    const ImVec2 corners[] = {
        p0,
        ImVec2(p1.x, p0.y),
        p1,
        ImVec2(p0.x, p1.y),
    };

    for (const ImVec2 corner : corners) {
        drawList->AddCircle(corner, 12.0f, IM_COL32(255, 220, 90, 130), 16, 1.0f);
    }

    const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    drawList->AddCircleFilled(center, 8.0f, moveFill, 16);
    drawList->AddCircle(center, 8.0f, boxColor, 16, 1.5f);
}

static void DrawSelectedVertexTransformBox(
    ImDrawList* drawList,
    const EditorState& editor,
    ImVec2 imagePos,
    float zoom
) {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    if (!GetDeformVertexSelectionBounds(editor, x0, y0, x1, y1)) {
        return;
    }

    PadBounds(x0, y0, x1, y1, zoom);
    const ImVec2 p0 = CanvasToScreenPoint(Vec2{x0, y0}, imagePos, zoom);
    const ImVec2 p1 = CanvasToScreenPoint(Vec2{x1, y1}, imagePos, zoom);
    const ImU32 boxColor = IM_COL32(90, 210, 255, 240);
    const ImU32 handleFill = IM_COL32(25, 26, 30, 230);
    const ImU32 moveFill = IM_COL32(8, 9, 12, 255);
    const float handle = 5.0f;

    drawList->AddRect(p0, p1, boxColor, 0.0f, 0, 1.5f);

    const ImVec2 edgeHandles[] = {
        ImVec2((p0.x + p1.x) * 0.5f, p0.y),
        ImVec2(p1.x, (p0.y + p1.y) * 0.5f),
        ImVec2((p0.x + p1.x) * 0.5f, p1.y),
        ImVec2(p0.x, (p0.y + p1.y) * 0.5f),
    };

    for (const ImVec2 h : edgeHandles) {
        drawList->AddRectFilled(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            handleFill
        );
        drawList->AddRect(
            ImVec2(h.x - handle, h.y - handle),
            ImVec2(h.x + handle, h.y + handle),
            boxColor
        );
    }

    const ImVec2 corners[] = {
        p0,
        ImVec2(p1.x, p0.y),
        p1,
        ImVec2(p0.x, p1.y),
    };

    for (const ImVec2 corner : corners) {
        drawList->AddCircle(corner, 12.0f, IM_COL32(90, 210, 255, 140), 16, 1.0f);
    }

    const ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    drawList->AddCircleFilled(center, 8.0f, moveFill, 16);
    drawList->AddCircle(center, 8.0f, boxColor, 16, 1.5f);
}
