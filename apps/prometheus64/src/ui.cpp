#include "ui.hpp"

#include <SDL3/SDL_surface.h>

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace p64 {
namespace {

constexpr int kAtlasW = 2048, kAtlasH = 2048;  // two fonts, one half each
constexpr float kBakePx = 48.0f;  // rasterised large and scaled down, with 2x oversampling
// Space around each glyph in the atlas. Small text is drawn from the atlas's shrunken copies
// (mipmaps), where a 1-pixel gap vanishes and neighbouring glyphs bleed in as faint ghost letters;
// 8 pixels stays clear down to the 1/8 copy, the smallest that kMaxMip lets text use.
constexpr int kGlyphPadding = 8, kMaxMip = 3;

const char* kVertex = R"(#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_mode;
layout(location = 4) in vec2 a_local;
layout(location = 5) in vec4 a_shape;  // w, h, radius, thickness
uniform vec2 u_screen;
out vec2 v_uv; out vec4 v_color; out float v_mode; out vec2 v_local; out vec4 v_shape;
void main() {
  v_uv = a_uv; v_color = a_color; v_mode = a_mode; v_local = a_local; v_shape = a_shape;
  gl_Position = vec4(a_pos.x / u_screen.x * 2.0 - 1.0, 1.0 - a_pos.y / u_screen.y * 2.0, 0.0, 1.0);
}
)";

// mode 0: text (atlas alpha), 1: rounded box, 2: rounded outline, 3: image.
const char* kFragment = R"(#version 330 core
uniform sampler2D u_tex;
in vec2 v_uv; in vec4 v_color; in float v_mode; in vec2 v_local; in vec4 v_shape;
out vec4 o;
float box(vec2 p, vec2 half_size, float r) {
  vec2 q = abs(p - half_size) - half_size + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
  if (v_mode < 0.5) {
    o = vec4(v_color.rgb, v_color.a * texture(u_tex, v_uv).r);
    return;
  }
  vec2 half_size = v_shape.xy * 0.5;
  float d = box(v_local, half_size, min(v_shape.z, min(half_size.x, half_size.y)));
  float cover = clamp(0.5 - d, 0.0, 1.0);
  if (v_mode > 1.5 && v_mode < 2.5) cover *= clamp(0.5 + d + v_shape.w, 0.0, 1.0);
  if (v_mode > 2.5) {
    vec4 t = texture(u_tex, v_uv);
    o = vec4(t.rgb, t.a * v_color.a * cover);
    return;
  }
  o = vec4(v_color.rgb, v_color.a * cover);
}
)";

GLuint build_program(std::string& error) {
  auto compile = [&](GLenum type, const char* src) {
    GLuint s = gl::glCreateShader(type);
    gl::glShaderSource(s, 1, &src, nullptr);
    gl::glCompileShader(s);
    GLint ok = 0;
    gl::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[2048] = {};
      gl::glGetShaderInfoLog(s, sizeof log, nullptr, log);
      error = std::string("ui shader: ") + log;
    }
    return s;
  };
  GLuint vs = compile(GL_VERTEX_SHADER, kVertex), fs = compile(GL_FRAGMENT_SHADER, kFragment);
  GLuint p = gl::glCreateProgram();
  gl::glAttachShader(p, vs);
  gl::glAttachShader(p, fs);
  gl::glLinkProgram(p);
  gl::glDeleteShader(vs);
  gl::glDeleteShader(fs);
  return p;
}

// Decodes UTF-8, mapping anything outside Latin-1 to '?'.
template <typename F>
void each_char(const std::string& s, F f) {
  for (std::size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp = c;
    int len = 1;
    if (c >= 0xF0) {
      len = 4;
    } else if (c >= 0xE0) {
      len = 3;
    } else if (c >= 0xC0) {
      len = 2;
      if (i + 1 < s.size()) cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
    }
    if (len > 2 || cp > 255) cp = '?';
    if (cp < 32) cp = ' ';
    f(cp);
    i += len;
  }
}

}  // namespace

Color rgb(uint32_t hex, float alpha) {
  return {((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, alpha};
}

bool Ui::bake(const std::filesystem::path& file, Font& font, int atlas_y, std::vector<uint8_t>& atlas,
              std::string& error) {
  std::ifstream in(file, std::ios::binary);
  std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (ttf.empty()) {
    error = "missing font: " + file.string();
    return false;
  }
  stbtt_pack_context pc;
  if (!stbtt_PackBegin(&pc, atlas.data() + static_cast<std::size_t>(atlas_y) * kAtlasW, kAtlasW, kAtlasH / 2, kAtlasW, kGlyphPadding,
                       nullptr)) {
    error = "font atlas";
    return false;
  }
  stbtt_PackSetOversampling(&pc, 2, 2);
  std::vector<stbtt_packedchar> chars(224);
  const int packed = stbtt_PackFontRange(&pc, ttf.data(), 0, kBakePx, 32, 224, chars.data());
  stbtt_PackEnd(&pc);
  if (!packed) {
    error = "font atlas too small for " + file.filename().string();
    return false;
  }

  stbtt_fontinfo info;
  stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0));
  int ascent = 0, descent = 0, gap = 0;
  stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
  font.px = kBakePx;
  font.ascent = ascent * stbtt_ScaleForPixelHeight(&info, kBakePx);
  font.glyphs.resize(224);
  for (int i = 0; i < 224; ++i) {
    const auto& c = chars[i];
    font.glyphs[i] = {static_cast<float>(c.x0), static_cast<float>(c.y0 + atlas_y), static_cast<float>(c.x1),
                      static_cast<float>(c.y1 + atlas_y), c.xoff, c.yoff, c.xoff2, c.yoff2, c.xadvance};
  }
  return true;
}

// Loads a PNG into a mipmapped texture, so it stays smooth when drawn much smaller. 0 if missing.
static GLuint load_image(const std::filesystem::path& file) {
  GLuint tex = 0;
  if (SDL_Surface* s = SDL_LoadPNG(file.string().c_str())) {
    if (SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888)) {
      gl::glGenTextures(1, &tex);
      gl::glBindTexture(GL_TEXTURE_2D, tex);
      gl::glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
      gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
      gl::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
      gl::glGenerateMipmap(GL_TEXTURE_2D);
      gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      SDL_DestroySurface(rgba);
    }
    SDL_DestroySurface(s);
  }
  return tex;
}

bool Ui::init(const std::filesystem::path& assets, std::string& error) {
  std::vector<uint8_t> atlas(static_cast<std::size_t>(kAtlasW) * kAtlasH, 0);
  if (!bake(assets / "fonts" / "FiraSans-Regular.ttf", regular_, 0, atlas, error)) return false;
  if (!bake(assets / "fonts" / "FiraSans-SemiBold.ttf", bold_, kAtlasH / 2, atlas, error)) return false;

  gl::glGenTextures(1, &atlas_tex_);
  gl::glBindTexture(GL_TEXTURE_2D, atlas_tex_);
  gl::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasW, kAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, kMaxMip);
  gl::glGenerateMipmap(GL_TEXTURE_2D);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  logo_tex_ = load_image(assets / "icon.png");
  logo_small_tex_ = load_image(assets / "icon-small.png");

  program_ = build_program(error);
  if (!error.empty()) return false;
  u_screen_ = gl::glGetUniformLocation(program_, "u_screen");
  gl::glGenVertexArrays(1, &vao_);
  gl::glGenBuffers(1, &vbo_);
  gl::glBindVertexArray(vao_);
  gl::glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  const GLsizei stride = sizeof(Vertex);
  auto attr = [&](GLuint index, GLint count, std::size_t offset) {
    gl::glEnableVertexAttribArray(index);
    gl::glVertexAttribPointer(index, count, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offset));
  };
  attr(0, 2, offsetof(Vertex, x));
  attr(1, 2, offsetof(Vertex, u));
  attr(2, 4, offsetof(Vertex, r));
  attr(3, 1, offsetof(Vertex, mode));
  attr(4, 2, offsetof(Vertex, lx));
  attr(5, 4, offsetof(Vertex, w));
  gl::glBindVertexArray(0);
  gl::glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

void Ui::shutdown() {
  if (atlas_tex_) gl::glDeleteTextures(1, &atlas_tex_);
  if (logo_tex_) gl::glDeleteTextures(1, &logo_tex_);
  if (logo_small_tex_) gl::glDeleteTextures(1, &logo_small_tex_);
  if (vbo_) gl::glDeleteBuffers(1, &vbo_);
  if (vao_) gl::glDeleteVertexArrays(1, &vao_);
  if (program_) gl::glDeleteProgram(program_);
  atlas_tex_ = logo_tex_ = logo_small_tex_ = vbo_ = vao_ = program_ = 0;
}

void Ui::begin(int width, int height) {
  width_ = width;
  height_ = height;
  batch_.clear();
  bound_tex_ = atlas_tex_;
  gl::glEnable(GL_BLEND);
  gl::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Ui::end() {
  flush();
  gl::glDisable(GL_BLEND);
}

void Ui::use_texture(GLuint tex) {
  if (tex == bound_tex_) return;
  flush();
  bound_tex_ = tex;
}

void Ui::flush() {
  if (batch_.empty()) return;
  gl::glUseProgram(program_);
  gl::glUniform2f(u_screen_, static_cast<float>(width_), static_cast<float>(height_));
  gl::glUniform1i(gl::glGetUniformLocation(program_, "u_tex"), 0);
  gl::glActiveTexture(GL_TEXTURE0);
  gl::glBindTexture(GL_TEXTURE_2D, bound_tex_);
  gl::glBindVertexArray(vao_);
  gl::glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  gl::glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch_.size() * sizeof(Vertex)), batch_.data(),
                   GL_STREAM_DRAW);
  gl::glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch_.size()));
  gl::glBindVertexArray(0);
  gl::glBindBuffer(GL_ARRAY_BUFFER, 0);
  batch_.clear();
}

void Ui::quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color top, Color bottom,
              float mode, float radius, float thickness) {
  auto v = [&](float px, float py, float u, float vv, Color c, float lx, float ly) {
    batch_.push_back({px, py, u, vv, c.r, c.g, c.b, c.a, mode, lx, ly, w, h, radius, thickness});
  };
  v(x, y, u0, v0, top, 0, 0);
  v(x + w, y, u1, v0, top, w, 0);
  v(x, y + h, u0, v1, bottom, 0, h);
  v(x + w, y, u1, v0, top, w, 0);
  v(x + w, y + h, u1, v1, bottom, w, h);
  v(x, y + h, u0, v1, bottom, 0, h);
}

void Ui::rect(float x, float y, float w, float h, Color c, float radius) {
  quad(x, y, w, h, 0, 0, 0, 0, c, c, 1, radius, 0);
}

void Ui::gradient(float x, float y, float w, float h, Color top, Color bottom, float radius) {
  quad(x, y, w, h, 0, 0, 0, 0, top, bottom, 1, radius, 0);
}

void Ui::outline(float x, float y, float w, float h, Color c, float radius, float thickness) {
  quad(x, y, w, h, 0, 0, 0, 0, c, c, 2, radius, thickness);
}

void Ui::image(GLuint texture, float x, float y, float w, float h, float alpha) {
  if (!texture) return;
  use_texture(texture);
  quad(x, y, w, h, 0, 0, 1, 1, Color{1, 1, 1, alpha}, Color{1, 1, 1, alpha}, 3, 0, 0);
  use_texture(atlas_tex_);
}

float Ui::measure(const std::string& s, float size, bool bold) const {
  const Font& f = bold ? bold_ : regular_;
  const float k = size / f.px;
  float w = 0;
  each_char(s, [&](uint32_t cp) { w += f.glyphs[cp - 32].advance * k; });
  return w;
}

std::string Ui::fit(const std::string& s, float size, float max_width, bool bold) const {
  if (measure(s, size, bold) <= max_width) return s;
  std::string t = s;
  while (!t.empty() && measure(t + "...", size, bold) > max_width) {
    t.pop_back();
    while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
  }
  return t + "...";
}

float Ui::text(float x, float y, const std::string& s, float size, Color c, bool bold, Align align) {
  const Font& f = bold ? bold_ : regular_;
  const float k = size / f.px;
  const float w = measure(s, size, bold);
  if (align == Align::Center) x -= w / 2;
  if (align == Align::Right) x -= w;
  use_texture(atlas_tex_);
  const float baseline = y + f.ascent * k;
  float pen = x;
  each_char(s, [&](uint32_t cp) {
    const Glyph& g = f.glyphs[cp - 32];
    if (g.x1 > g.x0) {
      quad(pen + g.xoff * k, baseline + g.yoff * k, (g.xoff2 - g.xoff) * k, (g.yoff2 - g.yoff) * k, g.x0 / kAtlasW,
           g.y0 / kAtlasH, g.x1 / kAtlasW, g.y1 / kAtlasH, c, c, 0, 0, 0);
    }
    pen += g.advance * k;
  });
  return w;
}

}  // namespace p64
