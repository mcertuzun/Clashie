#include "renderer_opengl.h"

#if !defined(PEBBLE_PLATFORM_MACOS) && !defined(PEBBLE_PLATFORM_IOS)

#ifdef __EMSCRIPTEN__
    #include <GLES3/gl3.h>
    #include <emscripten.h>
#elif defined(PEBBLE_PLATFORM_WINDOWS)
    // On Windows, use glad for OpenGL 3.3 function loading
    #include <glad/glad.h>
#else
    // Fallback: assume glad
    #include <glad/glad.h>
#endif

#include "pebble/platform/platform.h"
#include <vector>
#include <cstring>
#include <cstdio>

namespace pebble::gfx {

// ---------------------------------------------------------------------------
// Shader sources
// ---------------------------------------------------------------------------

#ifdef __EMSCRIPTEN__
static const char* SHADER_VERSION = "#version 300 es\n";
#else
static const char* SHADER_VERSION = "#version 330 core\n";
#endif

static const char* VERTEX_SHADER_BODY = R"(
precision mediump float;

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texcoord;
layout(location = 2) in vec4 a_color;

uniform mat4 u_projection;

out vec2 v_texcoord;
out vec4 v_color;

void main() {
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_texcoord = a_texcoord;
    v_color = a_color / 255.0;
}
)";

static const char* FRAGMENT_TEXTURED_BODY = R"(
precision mediump float;

in vec2 v_texcoord;
in vec4 v_color;

uniform sampler2D u_texture;

out vec4 fragColor;

void main() {
    fragColor = texture(u_texture, v_texcoord) * v_color;
}
)";

static const char* FRAGMENT_UNTEXTURED_BODY = R"(
precision mediump float;

in vec2 v_texcoord;
in vec4 v_color;

out vec4 fragColor;

void main() {
    fragColor = v_color;
}
)";

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static GLuint compile_shader(GLenum type, const char* version, const char* body) {
    GLuint shader = glCreateShader(type);
    const char* sources[2] = { version, body };
    glShaderSource(shader, 2, sources, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        pebble::platform::log_error("OpenGL shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint link_program(GLuint vert, GLuint frag) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        pebble::platform::log_error("OpenGL program link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ---------------------------------------------------------------------------
// Texture entry
// ---------------------------------------------------------------------------

struct TextureEntry {
    GLuint texture;
    bool   active;
};

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct OpenGLRenderer::Impl {
    // Shader programs
    GLuint prog_textured   = 0;
    GLuint prog_untextured = 0;

    // Uniform locations
    GLint u_proj_textured   = -1;
    GLint u_proj_untextured = -1;
    GLint u_texture_loc     = -1;

    // VAO / VBO / IBO
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;

    // Buffer sizes (bytes)
    size_t vb_capacity = 0;
    size_t ib_capacity = 0;

    // Ring buffer offsets (bytes) — reset each frame
    size_t vb_offset = 0;
    size_t ib_offset = 0;

    // Base offsets for the most recent upload (bytes)
    size_t current_vb_base = 0;
    size_t current_ib_base = 0;

    // White 1x1 fallback texture
    GLuint white_texture = 0;

    // Projection matrix (cached for deferred uniform updates)
    float projection[16];
    bool projection_dirty = true;

    // Texture table
    std::vector<TextureEntry> textures;
    uint32_t next_texture_id = 1;

    // Window handle for swap buffers
    WindowHandle window = 0;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

OpenGLRenderer::~OpenGLRenderer() {
    shutdown();
}

bool OpenGLRenderer::init(const RendererConfig& config) {
    m_width  = config.width;
    m_height = config.height;

    m_impl = new Impl();
    m_impl->window = config.window;

#if !defined(__EMSCRIPTEN__)
    // On desktop, GLAD must be loaded before any GL calls.
    // The window system is expected to have created an OpenGL context already.
    if (!gladLoadGL()) {
        pebble::platform::log_error("OpenGLRenderer: failed to load OpenGL via GLAD");
        delete m_impl;
        m_impl = nullptr;
        return false;
    }
#endif

    // ------------------------------------------------------------------
    // Compile shaders and link programs
    // ------------------------------------------------------------------
    GLuint vs = compile_shader(GL_VERTEX_SHADER, SHADER_VERSION, VERTEX_SHADER_BODY);
    if (!vs) { shutdown(); return false; }

    GLuint fs_tex = compile_shader(GL_FRAGMENT_SHADER, SHADER_VERSION, FRAGMENT_TEXTURED_BODY);
    if (!fs_tex) { glDeleteShader(vs); shutdown(); return false; }

    GLuint fs_notex = compile_shader(GL_FRAGMENT_SHADER, SHADER_VERSION, FRAGMENT_UNTEXTURED_BODY);
    if (!fs_notex) { glDeleteShader(vs); glDeleteShader(fs_tex); shutdown(); return false; }

    m_impl->prog_textured = link_program(vs, fs_tex);
    m_impl->prog_untextured = link_program(vs, fs_notex);

    // Shaders can be deleted after linking
    glDeleteShader(vs);
    glDeleteShader(fs_tex);
    glDeleteShader(fs_notex);

    if (!m_impl->prog_textured || !m_impl->prog_untextured) {
        shutdown();
        return false;
    }

    // Cache uniform locations
    m_impl->u_proj_textured   = glGetUniformLocation(m_impl->prog_textured,   "u_projection");
    m_impl->u_proj_untextured = glGetUniformLocation(m_impl->prog_untextured, "u_projection");
    m_impl->u_texture_loc     = glGetUniformLocation(m_impl->prog_textured,   "u_texture");

    // ------------------------------------------------------------------
    // Create VAO, VBO, IBO
    // ------------------------------------------------------------------
    // 2x headroom, same sizing as Metal renderer
    m_impl->vb_capacity = 2 * 4096 * 4 * sizeof(SpriteVertex);
    m_impl->ib_capacity = 2 * 4096 * 6 * sizeof(uint16_t);

    glGenVertexArrays(1, &m_impl->vao);
    glBindVertexArray(m_impl->vao);

    glGenBuffers(1, &m_impl->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_impl->vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_impl->vb_capacity), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_impl->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_impl->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_impl->ib_capacity), nullptr, GL_DYNAMIC_DRAW);

    // Vertex layout — matches SpriteVertex (20 bytes)
    // attribute 0: position  float2  offset 0
    // attribute 1: texcoord  float2  offset 8
    // attribute 2: color     ubyte4  offset 16 (normalized)
    GLsizei stride = static_cast<GLsizei>(sizeof(SpriteVertex));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, reinterpret_cast<void*>(16));

    glBindVertexArray(0);

    // ------------------------------------------------------------------
    // Create 1x1 white fallback texture
    // ------------------------------------------------------------------
    uint8_t white_pixel[] = { 255, 255, 255, 255 };
    TextureID white_id = texture_create(1, 1, white_pixel);
    if (white_id > 0 && white_id <= static_cast<uint32_t>(m_impl->textures.size())) {
        m_impl->white_texture = m_impl->textures[white_id - 1].texture;
    }

    // ------------------------------------------------------------------
    // Default GL state
    // ------------------------------------------------------------------
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    // Initialize projection to identity
    memset(m_impl->projection, 0, sizeof(m_impl->projection));
    m_impl->projection[0]  = 1.0f;
    m_impl->projection[5]  = 1.0f;
    m_impl->projection[10] = 1.0f;
    m_impl->projection[15] = 1.0f;
    m_impl->projection_dirty = true;

    pebble::platform::log_info("OpenGL renderer initialized (%dx%d)", m_width, m_height);

    return true;
}

void OpenGLRenderer::shutdown() {
    if (!m_impl) return;

    // Destroy textures
    for (auto& entry : m_impl->textures) {
        if (entry.active && entry.texture) {
            glDeleteTextures(1, &entry.texture);
        }
    }

    if (m_impl->vao) glDeleteVertexArrays(1, &m_impl->vao);
    if (m_impl->vbo) glDeleteBuffers(1, &m_impl->vbo);
    if (m_impl->ibo) glDeleteBuffers(1, &m_impl->ibo);

    if (m_impl->prog_textured)   glDeleteProgram(m_impl->prog_textured);
    if (m_impl->prog_untextured) glDeleteProgram(m_impl->prog_untextured);

    delete m_impl;
    m_impl = nullptr;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void OpenGLRenderer::begin_frame(float r, float g, float b, float a) {
    // Reset ring buffer offsets for the new frame
    m_impl->vb_offset = 0;
    m_impl->ib_offset = 0;
    m_impl->current_vb_base = 0;
    m_impl->current_ib_base = 0;

    glViewport(0, 0, m_width, m_height);
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void OpenGLRenderer::end_frame() {
    // Swap buffers via the platform layer
    pebble::platform::window_swap_buffers(m_impl->window);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

TextureID OpenGLRenderer::texture_create(int32_t width, int32_t height, const uint8_t* rgba_data) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 0;

    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);

    glBindTexture(GL_TEXTURE_2D, 0);

    TextureID id = m_impl->next_texture_id++;
    while (m_impl->textures.size() < id) {
        m_impl->textures.push_back({ 0, false });
    }
    m_impl->textures[id - 1] = { tex, true };
    return id;
}

void OpenGLRenderer::texture_destroy(TextureID id) {
    if (id == 0 || id > static_cast<uint32_t>(m_impl->textures.size())) return;
    auto& entry = m_impl->textures[id - 1];
    if (entry.active && entry.texture) {
        glDeleteTextures(1, &entry.texture);
    }
    entry.texture = 0;
    entry.active = false;
}

// ---------------------------------------------------------------------------
// Batch upload (ring buffer)
// ---------------------------------------------------------------------------

void OpenGLRenderer::upload_batch_data(const SpriteVertex* vertices, uint32_t vertex_count,
                                        const uint16_t* indices, uint32_t index_count) {
    if (vertex_count == 0) return;

    size_t vb_bytes = vertex_count * sizeof(SpriteVertex);
    size_t ib_bytes = index_count * sizeof(uint16_t);

    // Safety: don't overflow buffers
    if (m_impl->vb_offset + vb_bytes > m_impl->vb_capacity ||
        m_impl->ib_offset + ib_bytes > m_impl->ib_capacity) {
        pebble::platform::log_error("OpenGLRenderer: ring buffer overflow (vb=%zu+%zu/%zu, ib=%zu+%zu/%zu)",
            m_impl->vb_offset, vb_bytes, m_impl->vb_capacity,
            m_impl->ib_offset, ib_bytes, m_impl->ib_capacity);
        return;
    }

    // Remember base offsets for this upload's draw calls
    m_impl->current_vb_base = m_impl->vb_offset;
    m_impl->current_ib_base = m_impl->ib_offset;

    // Upload vertex data at current offset
    glBindBuffer(GL_ARRAY_BUFFER, m_impl->vbo);
    glBufferSubData(GL_ARRAY_BUFFER,
                    static_cast<GLintptr>(m_impl->vb_offset),
                    static_cast<GLsizeiptr>(vb_bytes),
                    vertices);

    // Upload index data at current offset
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_impl->ibo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                    static_cast<GLintptr>(m_impl->ib_offset),
                    static_cast<GLsizeiptr>(ib_bytes),
                    indices);

    // Advance ring buffer offsets for next upload
    m_impl->vb_offset += vb_bytes;
    m_impl->ib_offset += ib_bytes;
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void OpenGLRenderer::draw_batch(uint32_t index_offset, uint32_t index_count, TextureID texture) {
    if (index_count == 0) return;

    GLuint gl_tex = m_impl->white_texture;
    bool has_texture = false;

    if (texture > 0 && texture <= static_cast<uint32_t>(m_impl->textures.size())
        && m_impl->textures[texture - 1].active) {
        gl_tex = m_impl->textures[texture - 1].texture;
        has_texture = true;
    }

    // Select program
    GLuint prog;
    GLint u_proj_loc;
    if (has_texture) {
        prog = m_impl->prog_textured;
        u_proj_loc = m_impl->u_proj_textured;
    } else {
        prog = m_impl->prog_untextured;
        u_proj_loc = m_impl->u_proj_untextured;
    }

    glUseProgram(prog);

    // Upload projection uniform (both programs share the same matrix)
    glUniformMatrix4fv(u_proj_loc, 1, GL_FALSE, m_impl->projection);

    if (has_texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gl_tex);
        glUniform1i(m_impl->u_texture_loc, 0);
    }

    // Bind VAO — vertex attrib pointers are pre-configured but we need to
    // offset the base vertex to account for ring buffer position.
    glBindVertexArray(m_impl->vao);

    // Compute the vertex base offset in number of vertices
    // current_vb_base is the byte offset of the current upload's vertices
    uint32_t base_vertex = static_cast<uint32_t>(m_impl->current_vb_base / sizeof(SpriteVertex));

    // Index offset is relative to the current upload's index base in the ring buffer
    size_t absolute_ib_byte_offset = m_impl->current_ib_base + index_offset * sizeof(uint16_t);

    // glDrawElementsBaseVertex allows offsetting vertex indices without modifying index data
#ifdef __EMSCRIPTEN__
    // WebGL 2.0 / GLES 3.0 does not have glDrawElementsBaseVertex.
    // Instead, we re-bind the VBO with an offset and use glDrawElements.
    // The VAO attribute pointers use offset 0, so we reconfigure them to include
    // the base vertex offset.
    glBindBuffer(GL_ARRAY_BUFFER, m_impl->vbo);
    GLsizei stride = static_cast<GLsizei>(sizeof(SpriteVertex));
    size_t vb_byte_base = m_impl->current_vb_base;

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(vb_byte_base + 0));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(vb_byte_base + 8));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride,
                          reinterpret_cast<void*>(vb_byte_base + 16));

    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(index_count),
                   GL_UNSIGNED_SHORT,
                   reinterpret_cast<void*>(absolute_ib_byte_offset));
#else
    glDrawElementsBaseVertex(GL_TRIANGLES,
                             static_cast<GLsizei>(index_count),
                             GL_UNSIGNED_SHORT,
                             reinterpret_cast<void*>(absolute_ib_byte_offset),
                             static_cast<GLint>(base_vertex));
#endif

    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------

void OpenGLRenderer::set_projection(const float* matrix) {
    memcpy(m_impl->projection, matrix, sizeof(float) * 16);
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

Renderer* create_renderer() {
    return new OpenGLRenderer();
}

} // namespace pebble::gfx

#endif // !PEBBLE_PLATFORM_MACOS && !PEBBLE_PLATFORM_IOS
