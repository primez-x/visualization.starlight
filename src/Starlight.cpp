#include "Spectrum.h"
#include <kodi/addon-instance/Visualization.h>
#include <GLES2/gl2.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <mutex>

namespace
{
constexpr char VertexShader[] = R"(
attribute vec2 a_position;
attribute vec2 a_local;
attribute float a_length;
varying vec2 v_local;
varying float v_length;
void main()
{
  gl_Position = vec4(a_position, 0.0, 1.0);
  v_local = a_local;
  v_length = a_length;
}
)";

constexpr char FragmentShader[] = R"(
precision mediump float;
varying vec2 v_local;
varying float v_length;
void main()
{
  vec2 p = vec2(v_local.x, v_local.y - clamp(v_local.y, 0.0, v_length));
  float distance = length(p);
  float core = 1.0 - smoothstep(1.3, 2.4, distance);
  float glow = 0.18 * pow(1.0 - smoothstep(1.8, 9.0, distance), 2.0);
  float alpha = core + glow * (1.0 - core);
  vec3 gold = vec3(0.961, 0.761, 0.478);
  vec3 frost = vec3(0.957, 0.969, 0.984);
  vec3 color = mix(gold, frost, clamp(v_local.y / 105.0, 0.0, 1.0));
  gl_FragColor = vec4(color * alpha, alpha);
}
)";

GLuint Compile(GLenum type, const char* source)
{
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok == GL_TRUE)
    return shader;
  std::array<char, 1024> message{};
  glGetShaderInfoLog(shader, message.size(), nullptr, message.data());
  kodi::Log(ADDON_LOG_ERROR, "Starlight shader compile failed: %s", message.data());
  glDeleteShader(shader);
  return 0;
}

struct Vertex
{
  float x, y, localX, localY, length;
};

class GLState
{
public:
  GLState()
  {
    glGetIntegerv(GL_CURRENT_PROGRAM, &m_program);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_buffer);
    const GLenum names[] = {GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA,
                            GL_BLEND_DST_ALPHA, GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA};
    for (size_t n = 0; n < m_blend.size(); ++n)
      glGetIntegerv(names[n], &m_blend[n]);
    m_depth = glIsEnabled(GL_DEPTH_TEST);
    m_cull = glIsEnabled(GL_CULL_FACE);
    m_enabled = glIsEnabled(GL_BLEND);
    for (GLuint n = 0; n < m_attributes.size(); ++n)
    {
      auto& a = m_attributes[n];
      glGetVertexAttribiv(n, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a.enabled);
      glGetVertexAttribiv(n, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a.buffer);
      glGetVertexAttribiv(n, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a.size);
      glGetVertexAttribiv(n, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a.type);
      glGetVertexAttribiv(n, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a.normalized);
      glGetVertexAttribiv(n, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a.stride);
      glGetVertexAttribPointerv(n, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a.pointer);
    }
  }
  ~GLState()
  {
    for (GLuint n = 0; n < m_attributes.size(); ++n)
    {
      const auto& a = m_attributes[n];
      glBindBuffer(GL_ARRAY_BUFFER, a.buffer);
      glVertexAttribPointer(n, a.size, a.type, a.normalized, a.stride, a.pointer);
      if (a.enabled) glEnableVertexAttribArray(n); else glDisableVertexAttribArray(n);
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    glUseProgram(m_program);
    glBlendFuncSeparate(m_blend[0], m_blend[1], m_blend[2], m_blend[3]);
    glBlendEquationSeparate(m_blend[4], m_blend[5]);
    if (m_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (m_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (m_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
  }
private:
  struct Attribute
  {
    GLint enabled, buffer, size, type, normalized, stride;
    void* pointer;
  };
  GLint m_program = 0, m_buffer = 0;
  std::array<GLint, 6> m_blend{};
  std::array<Attribute, 3> m_attributes{};
  GLboolean m_depth = false, m_cull = false, m_enabled = false;
};

class Starlight final : public kodi::addon::CInstanceVisualization
{
public:
  explicit Starlight(const kodi::addon::IInstanceInfo& instance)
    : CInstanceVisualization(instance)
  {
    for (size_t bar = 0; bar < starlight::Spectrum::Bars; ++bar)
    {
      const float angle = 6.28318530718f * bar / starlight::Spectrum::Bars - 1.57079632679f;
      m_cos[bar] = std::cos(angle);
      m_sin[bar] = std::sin(angle);
    }
  }

  ~Starlight() override
  {
    if (m_buffer)
      glDeleteBuffers(1, &m_buffer);
    if (m_program)
      glDeleteProgram(m_program);
  }

  bool Start(int channels, int sampleRate, int, const std::string&) override
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_display.fill(0);
    m_frame = Clock::now();
    m_audio = m_frame;
    return m_spectrum.Reset(channels, sampleRate);
  }

  void AudioData(const float* data, size_t count) override
  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_spectrum.Push(data, count);
    m_audio = Clock::now();
  }

  bool IsDirty() override
  {
    return Clock::now() - m_frame >= std::chrono::milliseconds(33);
  }

  void Render() override
  {
    const GLState state;
    if (!m_attempted)
    {
      m_attempted = true;
      if (!Initialize())
        return;
    }
    if (!m_program || !m_buffer || Width() <= 0 || Height() <= 0)
      return;

    const auto now = Clock::now();
    const float dt = std::clamp(std::chrono::duration<float>(now - m_frame).count(), 0.0f, .1f);
    m_frame = now;
    std::array<float, starlight::Spectrum::Bars> target;
    {
      std::lock_guard<std::mutex> guard(m_mutex);
      target = m_spectrum.Levels();
      if (now - m_audio > std::chrono::milliseconds(200))
        target.fill(0);
    }
    const float attack = 1 - std::exp(-dt / .035f);
    const float release = 1 - std::exp(-dt / .18f);
    const float scale = std::min(Width(), Height()) / 720.0f;
    size_t index = 0;
    for (size_t bar = 0; bar < target.size(); ++bar)
    {
      m_display[bar] += (target[bar] - m_display[bar]) * (target[bar] > m_display[bar] ? attack : release);
      const float length = 5 + 100 * m_display[bar];
      const std::array<std::array<float, 2>, 4> corners{{
        {{-9, -9}}, {{9, -9}}, {{-9, length + 9}}, {{9, length + 9}}
      }};
      for (size_t corner : {0, 1, 2, 2, 1, 3})
      {
        const float x = corners[corner][0], y = corners[corner][1];
        const float px = (180 + y) * m_cos[bar] - x * m_sin[bar];
        const float py = (180 + y) * m_sin[bar] + x * m_cos[bar];
        m_vertices[index++] = {2 * px * scale / Width(), -2 * py * scale / Height(), x, y, length};
      }
    }

    // Kodi supplies the control viewport. Preserve each changed GLES state.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_program);
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(m_vertices), m_vertices.data());
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, localX)));
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, length)));
    glDrawArrays(GL_TRIANGLES, 0, m_vertices.size());
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
  }

private:
  bool Initialize()
  {
    const GLuint vertex = Compile(GL_VERTEX_SHADER, VertexShader);
    const GLuint fragment = Compile(GL_FRAGMENT_SHADER, FragmentShader);
    if (!vertex || !fragment)
    {
      if (vertex) glDeleteShader(vertex);
      if (fragment) glDeleteShader(fragment);
      return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, vertex);
    glAttachShader(m_program, fragment);
    glBindAttribLocation(m_program, 0, "a_position");
    glBindAttribLocation(m_program, 1, "a_local");
    glBindAttribLocation(m_program, 2, "a_length");
    glLinkProgram(m_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE)
    {
      std::array<char, 1024> message{};
      glGetProgramInfoLog(m_program, message.size(), nullptr, message.data());
      kodi::Log(ADDON_LOG_ERROR, "Starlight shader link failed: %s", message.data());
      glDeleteProgram(m_program);
      m_program = 0;
      return false;
    }
    glGenBuffers(1, &m_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(m_vertices), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    kodi::Log(ADDON_LOG_INFO, "Starlight GLES radial spectrum ready: %dx%d, 160 bars", Width(), Height());
    return m_buffer != 0;
  }

  using Clock = std::chrono::steady_clock;
  std::mutex m_mutex;
  starlight::Spectrum m_spectrum;
  std::array<float, starlight::Spectrum::Bars> m_display{}, m_cos{}, m_sin{};
  std::array<Vertex, starlight::Spectrum::Bars * 6> m_vertices{};
  Clock::time_point m_frame{}, m_audio{};
  GLuint m_program = 0, m_buffer = 0;
  bool m_attempted = false;
};

class Addon final : public kodi::addon::CAddonBase
{
public:
  ADDON_STATUS CreateInstance(const kodi::addon::IInstanceInfo& instance,
                              KODI_ADDON_INSTANCE_HDL& handle) override
  {
    if (!instance.IsType(ADDON_INSTANCE_VISUALIZATION))
      return ADDON_STATUS_NOT_IMPLEMENTED;
    handle = new Starlight(instance);
    return ADDON_STATUS_OK;
  }
};
}

ADDONCREATOR(Addon)
