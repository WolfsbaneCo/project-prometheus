#include "video.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace p64 {
namespace {

const char* kVertex = R"(#version 330 core
uniform vec4 u_dst;  // x0, y0, x1, y1 in clip space (y0 = bottom)
out vec2 v_uv;
void main() {
  vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
  v_uv = c;
  gl_Position = vec4(mix(u_dst.xy, u_dst.zw, c), 0.0, 1.0);
}
)";

// One shader for the game picture and for the blurred "ambient" background behind it.
// The picture is resampled with a Catmull-Rom (bicubic) filter: exact at 1:1, sharper than
// bilinear when it is scaled. Only a frame more than twice the output size uses the mipmaps.
const char* kFragment = R"(#version 330 core
uniform sampler2D u_tex;
uniform vec4 u_src;      // texture rect mapped onto the quad: x0, y0 (bottom), x1, y1 (top)
uniform float u_mode;    // 0 = picture, 1 = ambient background
uniform float u_lod;     // mip level for the background blur
uniform float u_dim;     // background darkening
uniform vec2 u_step;     // one output pixel, in texture units
uniform float u_sharpen;
uniform float u_scan;    // scanline strength
uniform float u_lines;   // source lines across the quad
uniform float u_alpha;
uniform float u_bicubic; // 1 = Catmull-Rom from the full-size frame
uniform vec2 u_size;     // texture size in texels
in vec2 v_uv;
out vec4 o;

// Catmull-Rom in 9 bilinear taps (weights folded into the sample positions).
vec3 catmull_rom(vec2 uv) {
  vec2 p = uv * u_size;
  vec2 t1 = floor(p - 0.5) + 0.5;
  vec2 f = p - t1;
  vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  vec2 w3 = f * f * (-0.5 + 0.5 * f);
  vec2 w12 = w1 + w2;
  vec2 t0 = (t1 - 1.0) / u_size, t3 = (t1 + 2.0) / u_size, t12 = (t1 + w2 / w12) / u_size;
  vec3 c = vec3(0.0);
  c += textureLod(u_tex, vec2(t0.x, t0.y), 0.0).rgb * w0.x * w0.y;
  c += textureLod(u_tex, vec2(t12.x, t0.y), 0.0).rgb * w12.x * w0.y;
  c += textureLod(u_tex, vec2(t3.x, t0.y), 0.0).rgb * w3.x * w0.y;
  c += textureLod(u_tex, vec2(t0.x, t12.y), 0.0).rgb * w0.x * w12.y;
  c += textureLod(u_tex, vec2(t12.x, t12.y), 0.0).rgb * w12.x * w12.y;
  c += textureLod(u_tex, vec2(t3.x, t12.y), 0.0).rgb * w3.x * w12.y;
  c += textureLod(u_tex, vec2(t0.x, t3.y), 0.0).rgb * w0.x * w3.y;
  c += textureLod(u_tex, vec2(t12.x, t3.y), 0.0).rgb * w12.x * w3.y;
  c += textureLod(u_tex, vec2(t3.x, t3.y), 0.0).rgb * w3.x * w3.y;
  return clamp(c, 0.0, 1.0);
}

void main() {
  vec2 uv = mix(u_src.xy, u_src.zw, v_uv);
  if (u_mode > 0.5) {
    o = vec4(textureLod(u_tex, uv, u_lod).rgb * u_dim, 1.0);
    return;
  }
  vec3 c = u_bicubic > 0.5 ? catmull_rom(uv) : texture(u_tex, uv).rgb;
  if (u_sharpen > 0.0) {
    vec3 n = texture(u_tex, uv + vec2(u_step.x, 0.0)).rgb + texture(u_tex, uv - vec2(u_step.x, 0.0)).rgb +
             texture(u_tex, uv + vec2(0.0, u_step.y)).rgb + texture(u_tex, uv - vec2(0.0, u_step.y)).rgb;
    c = clamp(c + u_sharpen * (c - 0.25 * n), 0.0, 1.0);
  }
  if (u_scan > 0.0) {
    float f = fract(v_uv.y * u_lines);
    float beam = 0.5 + 0.5 * cos(6.2831853 * (f - 0.5));
    c *= mix(1.0, 0.55 + 0.45 * beam, u_scan) * (1.0 + 0.18 * u_scan);
  }
  o = vec4(c, u_alpha);
}
)";

}  // namespace

const char* fill_mode_name(FillMode m) {
  switch (m) {
    case FillMode::Fit: return "Fit (4:3)";
    case FillMode::SmartZoom: return "Smart zoom";
    case FillMode::Widescreen: return "Widescreen";
    case FillMode::Ambient: return "Ambient fill";
    case FillMode::Stretch: return "Stretch (distorts the picture)";
    default: return "?";
  }
}

GLuint Video::compile(GLenum type, const char* src, std::string& error) {
  GLuint s = gl::glCreateShader(type);
  gl::glShaderSource(s, 1, &src, nullptr);
  gl::glCompileShader(s);
  GLint ok = 0;
  gl::glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048] = {};
    gl::glGetShaderInfoLog(s, sizeof log, nullptr, log);
    error = std::string("shader: ") + log;
  }
  return s;
}

bool Video::init(std::string& error) {
  GLuint vs = compile(GL_VERTEX_SHADER, kVertex, error);
  GLuint fs = compile(GL_FRAGMENT_SHADER, kFragment, error);
  if (!error.empty()) return false;
  program_ = gl::glCreateProgram();
  gl::glAttachShader(program_, vs);
  gl::glAttachShader(program_, fs);
  gl::glLinkProgram(program_);
  gl::glDeleteShader(vs);
  gl::glDeleteShader(fs);
  GLint ok = 0;
  gl::glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048] = {};
    gl::glGetProgramInfoLog(program_, sizeof log, nullptr, log);
    error = std::string("shader link: ") + log;
    return false;
  }
  u_src_ = gl::glGetUniformLocation(program_, "u_src");
  u_dst_ = gl::glGetUniformLocation(program_, "u_dst");
  u_mode_ = gl::glGetUniformLocation(program_, "u_mode");
  u_lod_ = gl::glGetUniformLocation(program_, "u_lod");
  u_bicubic_ = gl::glGetUniformLocation(program_, "u_bicubic");
  u_size_ = gl::glGetUniformLocation(program_, "u_size");
  u_dim_ = gl::glGetUniformLocation(program_, "u_dim");
  u_step_ = gl::glGetUniformLocation(program_, "u_step");
  u_sharpen_ = gl::glGetUniformLocation(program_, "u_sharpen");
  u_scan_ = gl::glGetUniformLocation(program_, "u_scan");
  u_lines_ = gl::glGetUniformLocation(program_, "u_lines");
  u_alpha_ = gl::glGetUniformLocation(program_, "u_alpha");
  gl::glGenVertexArrays(1, &vao_);
  gl::glGenTextures(1, &sw_tex_);
  return true;
}

void Video::shutdown() {
  if (target_fbo_) gl::glDeleteFramebuffers(1, &target_fbo_);
  if (target_tex_) gl::glDeleteTextures(1, &target_tex_);
  if (target_depth_) gl::glDeleteRenderbuffers(1, &target_depth_);
  if (sw_tex_) gl::glDeleteTextures(1, &sw_tex_);
  if (vao_) gl::glDeleteVertexArrays(1, &vao_);
  if (program_) gl::glDeleteProgram(program_);
  target_fbo_ = target_tex_ = target_depth_ = sw_tex_ = vao_ = program_ = 0;
  target_w_ = target_h_ = 0;
}

void Video::prepare_target(unsigned width, unsigned height) {
  if (target_fbo_ && width <= target_w_ && height <= target_h_) return;
  width = std::max(width, target_w_);
  height = std::max(height, target_h_);
  if (!target_fbo_) {
    gl::glGenFramebuffers(1, &target_fbo_);
    gl::glGenTextures(1, &target_tex_);
    gl::glGenRenderbuffers(1, &target_depth_);
  }
  gl::glBindTexture(GL_TEXTURE_2D, target_tex_);
  gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl::glGenerateMipmap(GL_TEXTURE_2D);
  gl::glBindRenderbuffer(GL_RENDERBUFFER, target_depth_);
  gl::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
  gl::glBindFramebuffer(GL_FRAMEBUFFER, target_fbo_);
  gl::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_tex_, 0);
  gl::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target_depth_);
  gl::glClearColor(0, 0, 0, 1);
  gl::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
  target_w_ = width;
  target_h_ = height;
}

void Video::hardware_frame(unsigned width, unsigned height) {
  frame_is_hw_ = true;
  frame_w_ = width;
  frame_h_ = height;
}

void Video::software_frame(const void* data, unsigned width, unsigned height, std::size_t pitch,
                           retro_pixel_format format) {
  gl::glBindTexture(GL_TEXTURE_2D, sw_tex_);
  GLenum fmt = GL_BGRA, type = GL_UNSIGNED_INT_8_8_8_8_REV;
  unsigned bpp = 4;
  if (format == RETRO_PIXEL_FORMAT_RGB565) {
    fmt = GL_RGB;
    type = GL_UNSIGNED_SHORT_5_6_5;
    bpp = 2;
  } else if (format == RETRO_PIXEL_FORMAT_0RGB1555) {
    fmt = GL_BGRA;
    type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
    bpp = 2;
  }
  if (width > sw_w_ || height > sw_h_) {
    sw_w_ = std::max(width, sw_w_);
    sw_h_ = std::max(height, sw_h_);
    gl::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sw_w_, sw_h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  gl::glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(pitch / bpp));
  gl::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl::glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, fmt, type, data);
  gl::glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  gl::glGenerateMipmap(GL_TEXTURE_2D);
  frame_is_hw_ = false;
  frame_w_ = width;
  frame_h_ = height;
}

void Video::begin_present(int out_w, int out_h) {
  gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
  gl::glViewport(0, 0, out_w, out_h);
  gl::glDisable(GL_DEPTH_TEST);
  gl::glDisable(GL_STENCIL_TEST);
  gl::glDisable(GL_SCISSOR_TEST);
  gl::glDisable(GL_CULL_FACE);
  gl::glDisable(GL_BLEND);
  gl::glBlendEquation(GL_FUNC_ADD);
  gl::glDisable(GL_POLYGON_OFFSET_FILL);
  gl::glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
  gl::glDisable(GL_MULTISAMPLE);
  gl::glDisable(GL_FRAMEBUFFER_SRGB);
  gl::glDisable(GL_DITHER);
  gl::glDisable(GL_RASTERIZER_DISCARD);
  gl::glDepthMask(GL_TRUE);
  gl::glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  gl::glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  gl::glActiveTexture(GL_TEXTURE0);
  gl::glBindSampler(0, 0);
  gl::glBindBuffer(GL_ARRAY_BUFFER, 0);
  gl::glClearColor(0.035f, 0.03f, 0.03f, 1.0f);
  gl::glClear(GL_COLOR_BUFFER_BIT);
}

void Video::draw_quad(const float src[4], const float dst[4], bool background, float lod, float dim, const Look& look,
                      float step_x, float step_y, float alpha) {
  gl::glUseProgram(program_);
  gl::glBindVertexArray(vao_);
  gl::glUniform4f(u_src_, src[0], src[1], src[2], src[3]);
  gl::glUniform4f(u_dst_, dst[0], dst[1], dst[2], dst[3]);
  gl::glUniform1f(u_mode_, background ? 1.0f : 0.0f);
  gl::glUniform1f(u_lod_, lod);
  gl::glUniform1f(u_dim_, dim);
  gl::glUniform2f(u_step_, step_x, step_y);
  gl::glUniform1f(u_sharpen_, background ? 0.0f : look.sharpen);
  gl::glUniform1f(u_scan_, background ? 0.0f : look.scanlines);
  gl::glUniform1f(u_alpha_, alpha);
  gl::glUniform1f(u_bicubic_, background ? 0.0f : bicubic_);
  gl::glUniform2f(u_size_, size_w_, size_h_);
  gl::glUniform1i(gl::glGetUniformLocation(program_, "u_tex"), 0);
  gl::glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

Rect Video::draw_game(int out_w, int out_h, const Look& look, float alpha) {
  Rect r;
  if (!has_frame() || out_w <= 0 || out_h <= 0) return r;

  const GLuint tex = frame_is_hw_ ? target_tex_ : sw_tex_;
  const float tw = static_cast<float>(frame_is_hw_ ? target_w_ : sw_w_);
  const float th = static_cast<float>(frame_is_hw_ ? target_h_ : sw_h_);
  gl::glBindTexture(GL_TEXTURE_2D, tex);
  if (frame_is_hw_) gl::glGenerateMipmap(GL_TEXTURE_2D);
  size_w_ = tw;
  size_h_ = th;
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, look.smooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
  gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, look.smooth ? GL_LINEAR : GL_NEAREST);

  // Visible part of the frame, in texture units, before fill-mode cropping.
  const float u1 = frame_w_ / tw, v1 = frame_h_ / th;
  const float cx = look.overscan * u1, cy = look.overscan * v1;
  float sx0 = cx, sx1 = u1 - cx, sy0 = cy, sy1 = v1 - cy;

  // Destination rectangle in window pixels.
  const float aspect = look.fill == FillMode::Widescreen ? 16.0f / 9.0f : look.aspect;
  float dw = static_cast<float>(out_w), dh = static_cast<float>(out_h);
  if (look.fill != FillMode::Stretch) {
    if (dw / dh > aspect) {
      dw = dh * aspect;
    } else {
      dh = dw / aspect;
    }
  }
  if (look.fill == FillMode::SmartZoom) {
    // Grow by up to 12% toward the empty sides and crop the same share off the top and bottom,
    // so proportions never change.
    const float grow = std::min(1.12f, static_cast<float>(out_w) / dw);
    if (grow > 1.0f) {
      dw *= grow;
      const float keep = 1.0f / grow;  // share of the height still visible
      const float mid = (sy0 + sy1) / 2, half = (sy1 - sy0) / 2 * keep;
      sy0 = mid - half;
      sy1 = mid + half;
    }
  }
  r.w = static_cast<int>(std::lround(dw));
  r.h = static_cast<int>(std::lround(dh));
  r.x = (out_w - r.w) / 2;
  r.y = (out_h - r.h) / 2;

  // Hardware frames are stored bottom-up; uploaded pixels top-down.
  auto src_rect = [&](float x0, float y0, float x1, float y1, float out[4]) {
    out[0] = x0;
    out[2] = x1;
    if (frame_is_hw_) {
      out[1] = y0;
      out[3] = y1;
    } else {
      out[1] = y1;
      out[3] = y0;
    }
  };

  if (look.fill == FillMode::Ambient && (r.w < out_w || r.h < out_h)) {
    // Fill the window with a heavily blurred, darkened copy, cropped (not stretched) to fit.
    const float out_aspect = static_cast<float>(out_w) / out_h;
    float bx0 = sx0, bx1 = sx1, by0 = sy0, by1 = sy1;
    if (out_aspect > aspect) {
      const float keep = aspect / out_aspect, mid = (by0 + by1) / 2, half = (by1 - by0) / 2 * keep;
      by0 = mid - half;
      by1 = mid + half;
    } else {
      const float keep = out_aspect / aspect, mid = (bx0 + bx1) / 2, half = (bx1 - bx0) / 2 * keep;
      bx0 = mid - half;
      bx1 = mid + half;
    }
    float src[4];
    src_rect(bx0, by0, bx1, by1, src);
    const float dst[4] = {-1, -1, 1, 1};
    const float lod = std::max(0.0f, std::log2(static_cast<float>(frame_h_) / 10.0f));
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    draw_quad(src, dst, true, lod, 0.32f, look, 0, 0, 1);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, look.smooth ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
  }

  float src[4];
  src_rect(sx0, sy0, sx1, sy1, src);
  const float dst[4] = {2.0f * r.x / out_w - 1.0f, 1.0f - 2.0f * (r.y + r.h) / out_h, 2.0f * (r.x + r.w) / out_w - 1.0f,
                        1.0f - 2.0f * r.y / out_h};
  const float step_x = (sx1 - sx0) / std::max(1, r.w), step_y = (sy1 - sy0) / std::max(1, r.h);
  // Bicubic unless the frame is more than twice the size it is shown at (then mipmaps avoid
  // shimmer). The sharp-pixel look keeps nearest-neighbour sampling.
  bicubic_ = look.smooth && static_cast<float>(r.h) >= 0.5f * (sy1 - sy0) * th ? 1.0f : 0.0f;
  gl::glUseProgram(program_);
  gl::glUniform1f(u_lines_, 240.0f * (sy1 - sy0) / v1);  // the N64's 240 lines, minus any crop
  if (alpha < 1.0f) {
    gl::glEnable(GL_BLEND);
    gl::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }
  draw_quad(src, dst, false, 0, 1, look, step_x, step_y, alpha);
  bicubic_ = 0.0f;
  gl::glDisable(GL_BLEND);
  return r;
}

bool Video::read_pixels(int w, int h, uint8_t* rgb) {
  // The core shares the context; drop any error it left behind so ours is the one we see.
  for (int i = 0; i < 16 && gl::glGetError() != GL_NO_ERROR; ++i) {
  }
  gl::glBindFramebuffer(GL_FRAMEBUFFER, 0);
  gl::glPixelStorei(GL_PACK_ALIGNMENT, 1);
  gl::glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
  return gl::glGetError() == GL_NO_ERROR;
}

}  // namespace p64
