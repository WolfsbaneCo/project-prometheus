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
// bilinear when it is scaled. A frame about twice the output size or more uses the mipmaps.
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
uniform float u_pixels;  // pixel art: 0 = plain, 1 = xBR smoothing, 2 = sharp pixels
uniform float u_scale;   // output pixels per texel
uniform float u_grid;    // handheld LCD grid strength
in vec2 v_uv;
out vec4 o;

// Pixel art made smooth for big screens: Hyllian's xBR-lv2 (corner C, smooth tips), ported from
// libretro's glsl-shaders/xbr/shaders/xbr-lv2.glsl.
//   Copyright (C) 2011-2016 Hyllian - sergiogdb@gmail.com
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
//   and associated documentation files (the "Software"), to deal in the Software without
//   restriction, including without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
//   Software is furnished to do so, subject to the following conditions: The above copyright
//   notice and this permission notice shall be included in all copies or substantial portions of
//   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
//   PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
//   LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
//   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//   DEALINGS IN THE SOFTWARE.
// The edges blend over one output pixel whatever the size (delta from the actual scale), and every
// sample is a texel centre, so the texture's own filtering makes no difference.
const vec3 rgbw = vec3(14.352, 28.176, 5.472);
const vec4 Ao = vec4(1.0, -1.0, -1.0, 1.0), Bo = vec4(1.0, 1.0, -1.0, -1.0), Co = vec4(1.5, 0.5, -0.5, 0.5);
const vec4 Ax = vec4(1.0, -1.0, -1.0, 1.0), Bx = vec4(0.5, 2.0, -0.5, -2.0), Cx = vec4(1.0, 1.0, -0.5, 0.0);
const vec4 Ay = vec4(1.0, -1.0, -1.0, 1.0), By = vec4(2.0, 0.5, -2.0, -0.5), Cy = vec4(2.0, 0.0, -1.0, 0.5);
const vec4 Ci = vec4(0.25);
vec4 xdf(vec4 a, vec4 b) { return abs(a - b); }
vec4 xdiff(vec4 a, vec4 b) { return vec4(notEqual(a, b)); }
vec4 xeq(vec4 a, vec4 b) { return step(xdf(a, b), vec4(15.0)); }
vec4 xneq(vec4 a, vec4 b) { return vec4(1.0) - xeq(a, b); }
vec4 xwd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
  return xdf(a, b) + xdf(a, c) + xdf(d, e) + xdf(d, f) + 4.0 * xdf(g, h);
}
float c_df(vec3 c1, vec3 c2) { vec3 d = abs(c1 - c2); return d.r + d.g + d.b; }
vec3 px(vec2 c) { return textureLod(u_tex, c / u_size, 0.0).rgb; }  // c: texel centre, in texels

vec3 xbr(vec2 uv) {
  vec2 p = uv * u_size;
  vec2 ctr = floor(p) + 0.5;
  vec2 fp = p - floor(p);
  vec3 A1 = px(ctr + vec2(-1, -2)), B1 = px(ctr + vec2(0, -2)), C1 = px(ctr + vec2(1, -2));
  vec3 A = px(ctr + vec2(-1, -1)), B = px(ctr + vec2(0, -1)), C = px(ctr + vec2(1, -1));
  vec3 D = px(ctr + vec2(-1, 0)), E = px(ctr), F = px(ctr + vec2(1, 0));
  vec3 G = px(ctr + vec2(-1, 1)), H = px(ctr + vec2(0, 1)), I = px(ctr + vec2(1, 1));
  vec3 G5 = px(ctr + vec2(-1, 2)), H5 = px(ctr + vec2(0, 2)), I5 = px(ctr + vec2(1, 2));
  vec3 A0 = px(ctr + vec2(-2, -1)), D0 = px(ctr + vec2(-2, 0)), G0 = px(ctr + vec2(-2, 1));
  vec3 C4 = px(ctr + vec2(2, -1)), F4 = px(ctr + vec2(2, 0)), I4 = px(ctr + vec2(2, 1));
  vec4 b = vec4(dot(B, rgbw), dot(D, rgbw), dot(H, rgbw), dot(F, rgbw));
  vec4 c = vec4(dot(C, rgbw), dot(A, rgbw), dot(G, rgbw), dot(I, rgbw));
  vec4 d = b.yzwx, e = vec4(dot(E, rgbw)), f = b.wxyz, g = c.zwxy, h = b.zwxy, i = c.wxyz;
  vec4 i4 = vec4(dot(I4, rgbw), dot(C1, rgbw), dot(A0, rgbw), dot(G5, rgbw));
  vec4 i5 = vec4(dot(I5, rgbw), dot(C4, rgbw), dot(A1, rgbw), dot(G0, rgbw));
  vec4 h5 = vec4(dot(H5, rgbw), dot(F4, rgbw), dot(B1, rgbw), dot(D0, rgbw));
  vec4 f4 = h5.yzwx;
  float s = max(u_scale, 1.0);
  vec4 delta = vec4(1.0 / s), delta_l = vec4(0.5 / s, 1.0 / s, 0.5 / s, 1.0 / s), delta_u = delta_l.yxwz;
  vec4 fx = Ao * fp.y + Bo * fp.x, fx_l = Ax * fp.y + Bx * fp.x, fx_u = Ay * fp.y + By * fp.x;
  vec4 irlv0 = xdiff(e, f) * xdiff(e, h);
  vec4 irlv1 = irlv0 * (xneq(f, b) * xneq(f, c) + xneq(h, d) * xneq(h, g) +
                        xeq(e, i) * (xneq(f, f4) * xneq(f, i4) + xneq(h, h5) * xneq(h, i5)) + xeq(e, g) + xeq(e, c));
  vec4 irlv2l = xdiff(e, g) * xdiff(d, g), irlv2u = xdiff(e, c) * xdiff(b, c);
  vec4 fx45i = clamp((fx + delta - Co - Ci) / (2.0 * delta), 0.0, 1.0);
  vec4 fx45 = clamp((fx + delta - Co) / (2.0 * delta), 0.0, 1.0);
  vec4 fx30 = clamp((fx_l + delta_l - Cx) / (2.0 * delta_l), 0.0, 1.0);
  vec4 fx60 = clamp((fx_u + delta_u - Cy) / (2.0 * delta_u), 0.0, 1.0);
  vec4 wd1 = xwd(e, c, g, i, h5, f4, h, f), wd2 = xwd(h, d, i5, f, i4, b, e, i);
  vec4 edri = step(wd1, wd2) * irlv0;
  vec4 edr = step(wd1 + vec4(0.1), wd2) * step(vec4(0.5), irlv1);
  vec4 edr_l = step(2.0 * xdf(f, g), xdf(h, c)) * irlv2l * edr;
  vec4 edr_u = step(2.0 * xdf(h, c), xdf(f, g)) * irlv2u * edr;
  fx45 = edr * fx45;
  fx30 = edr_l * fx30;
  fx60 = edr_u * fx60;
  fx45i = edri * fx45i;
  vec4 pxs = step(xdf(e, f), xdf(e, h));
  vec4 maximos = max(max(fx30, fx60), max(fx45, fx45i));
  vec3 res1 = E;
  res1 = mix(res1, mix(H, F, pxs.x), maximos.x);
  res1 = mix(res1, mix(B, D, pxs.z), maximos.z);
  vec3 res2 = E;
  res2 = mix(res2, mix(F, B, pxs.y), maximos.y);
  res2 = mix(res2, mix(D, H, pxs.w), maximos.w);
  return mix(res1, res2, step(c_df(E, res1), c_df(E, res2)));
}

// Crisp pixels at any size: each texel is a solid block, sampled at its centre, and only within
// the last half output pixel before an edge does the sample slide toward the neighbour (linear
// filtering does the blend). So at most one output pixel per edge is mixed, which keeps the
// blocks even at sizes like 4.5x without shimmering; at whole-number sizes none is.
vec3 sharp_pixels(vec2 uv) {
  vec2 p = uv * u_size;
  vec2 cell = floor(p);
  vec2 d = p - cell - 0.5;                       // from the texel's centre, -0.5..0.5
  float r = 0.5 - 0.5 / max(u_scale, 1.0);       // the part of the texel that is solid
  vec2 f = (d - clamp(d, -r, r)) * max(u_scale, 1.0) + 0.5;
  return textureLod(u_tex, (cell + f) / u_size, 0.0).rgb;
}

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
  vec3 c = u_pixels > 1.5 ? sharp_pixels(uv) : u_pixels > 0.5 ? xbr(uv) : u_bicubic > 0.5 ? catmull_rom(uv) : texture(u_tex, uv).rgb;
  if (u_grid > 0.0) {
    // A handheld's LCD: a thin dark gap around each pixel, brightened so the screen keeps its level.
    vec2 f = fract(uv * u_size);
    float w = clamp(1.6 / max(u_scale, 1.0), 0.0, 0.5);
    float m = smoothstep(0.0, w, f.x) * smoothstep(0.0, w, 1.0 - f.x) * smoothstep(0.0, w, f.y) * smoothstep(0.0, w, 1.0 - f.y);
    c *= mix(1.0 - 0.55 * u_grid, 1.0 + 0.12 * u_grid, m);
  }
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
  u_pixels_ = gl::glGetUniformLocation(program_, "u_pixels");
  u_scale_ = gl::glGetUniformLocation(program_, "u_scale");
  u_grid_ = gl::glGetUniformLocation(program_, "u_grid");
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

namespace {

// MMPX, "Style-Preserving Pixel Art Magnification" (Morgan McGuire and Mara Gagiu, JCGT 2021):
// each pixel becomes 2x2, rounding staircases into curves and diagonals while keeping single-pixel
// details, text and thin lines as drawn. It only copies colours, never mixes them. Twice gives 4x.
// Ported from the authors' published rules:
//   Copyright 2020 Morgan McGuire & Mara Gagiu.
//   Provided under the Open Source MIT license https://opensource.org/licenses/MIT
uint32_t luma(uint16_t c) { return ((c >> 11) & 31u) * 8u + ((c >> 5) & 63u) * 4u + (c & 31u) * 8u + 1u; }  // RGB565
uint32_t luma(uint32_t c) { return ((c >> 16) & 255u) + ((c >> 8) & 255u) + (c & 255u) + 1u; }               // XRGB8888

template <typename Px>
void mmpx2x(const Px* src, std::size_t stride, int w, int h, Px* dst) {
  auto at = [&](int x, int y) { return src[static_cast<std::size_t>(std::clamp(y, 0, h - 1)) * stride + std::clamp(x, 0, w - 1)]; };
  auto all2 = [](Px b, Px a0, Px a1) { return b == a0 && b == a1; };
  auto all3 = [](Px b, Px a0, Px a1, Px a2) { return b == a0 && b == a1 && b == a2; };
  auto all4 = [](Px b, Px a0, Px a1, Px a2, Px a3) { return b == a0 && b == a1 && b == a2 && b == a3; };
  auto any3 = [](Px b, Px a0, Px a1, Px a2) { return b == a0 || b == a1 || b == a2; };
  auto none2 = [](Px b, Px a0, Px a1) { return b != a0 && b != a1; };
  auto none4 = [](Px b, Px a0, Px a1, Px a2, Px a3) { return b != a0 && b != a1 && b != a2 && b != a3; };
  const std::size_t out = 2 * static_cast<std::size_t>(w);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const Px A = at(x - 1, y - 1), B = at(x, y - 1), C = at(x + 1, y - 1);
      const Px D = at(x - 1, y), E = at(x, y), F = at(x + 1, y);
      const Px G = at(x - 1, y + 1), H = at(x, y + 1), I = at(x + 1, y + 1);
      Px J = E, K = E, L = E, M = E;
      if (!(A == E && B == E && C == E && D == E && F == E && G == E && H == E && I == E)) {
        const Px P = at(x, y - 2), S = at(x, y + 2), Q = at(x - 2, y), R = at(x + 2, y);
        const uint32_t Bl = luma(B), Dl = luma(D), El = luma(E), Fl = luma(F), Hl = luma(H);
        // 1:1 slopes
        if ((D == B && D != H && D != F) && (El >= Dl || E == A) && any3(E, A, C, G) && (El < Dl || A != D || E != P || E != Q)) J = D;
        if ((B == F && B != D && B != H) && (El >= Bl || E == C) && any3(E, A, C, I) && (El < Bl || C != B || E != P || E != R)) K = B;
        if ((H == D && H != F && H != B) && (El >= Hl || E == G) && any3(E, A, G, I) && (El < Hl || G != H || E != S || E != Q)) L = H;
        if ((F == H && F != B && F != D) && (El >= Fl || E == I) && any3(E, C, G, I) && (El < Fl || I != H || E != R || E != S)) M = F;
        // Intersections
        if ((E != F && all4(E, C, I, D, Q) && all2(F, B, H)) && F != at(x + 3, y)) K = M = F;
        if ((E != D && all4(E, A, G, F, R) && all2(D, B, H)) && D != at(x - 3, y)) J = L = D;
        if ((E != H && all4(E, G, I, B, P) && all2(H, D, F)) && H != at(x, y + 3)) L = M = H;
        if ((E != B && all4(E, A, C, H, S) && all2(B, D, F)) && B != at(x, y - 3)) J = K = B;
        if (Bl < El && all4(E, G, H, I, S) && none4(E, A, D, C, F)) J = K = B;
        if (Hl < El && all4(E, A, B, C, P) && none4(E, D, G, I, F)) L = M = H;
        if (Fl < El && all4(E, A, D, G, Q) && none4(E, B, C, I, H)) K = M = F;
        if (Dl < El && all4(E, C, F, I, R) && none4(E, B, A, G, H)) J = L = D;
        // 2:1 slopes
        if (H != B) {
          if (H != A && H != E && H != C) {
            if (all3(H, G, F, R) && none2(H, D, at(x + 2, y - 1))) L = M;
            if (all3(H, I, D, Q) && none2(H, F, at(x - 2, y - 1))) M = L;
          }
          if (B != I && B != G && B != E) {
            if (all3(B, A, F, R) && none2(B, D, at(x + 2, y + 1))) J = K;
            if (all3(B, C, D, Q) && none2(B, F, at(x - 2, y + 1))) K = J;
          }
        }
        if (F != D) {
          if (D != I && D != E && D != C) {
            if (all3(D, A, H, S) && none2(D, B, at(x + 1, y + 2))) J = L;
            if (all3(D, G, B, P) && none2(D, H, at(x + 1, y - 2))) L = J;
          }
          if (F != E && F != A && F != G) {
            if (all3(F, C, H, S) && none2(F, B, at(x - 1, y + 2))) K = M;
            if (all3(F, I, B, P) && none2(F, H, at(x - 1, y - 2))) M = K;
          }
        }
      }
      Px* o = dst + 2 * static_cast<std::size_t>(y) * out + 2 * static_cast<std::size_t>(x);
      o[0] = J;
      o[1] = K;
      o[out] = L;
      o[out + 1] = M;
    }
  }
}

template <typename Px>
const void* round_up(const void* data, std::size_t pitch, unsigned w, unsigned h, std::vector<uint8_t>& a,
                     std::vector<uint8_t>& b, bool four) {
  a.resize(static_cast<std::size_t>(w) * h * 4 * sizeof(Px));
  b.resize(static_cast<std::size_t>(w) * h * 16 * sizeof(Px));
  const int iw = static_cast<int>(w), ih = static_cast<int>(h);
  mmpx2x(static_cast<const Px*>(data), pitch / sizeof(Px), iw, ih, reinterpret_cast<Px*>(a.data()));
  if (!four) return a.data();
  mmpx2x(reinterpret_cast<const Px*>(a.data()), 2 * static_cast<std::size_t>(w), 2 * iw, 2 * ih, reinterpret_cast<Px*>(b.data()));
  return b.data();
}

}  // namespace

void Video::software_frame(const void* data, unsigned width, unsigned height, std::size_t pitch,
                           retro_pixel_format format, int round_edges) {
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
  if (round_edges == 2 || round_edges == 4) {
    const bool four = round_edges == 4;
    data = bpp == 2 ? round_up<uint16_t>(data, pitch, width, height, round_a_, round_b_, four)
                    : round_up<uint32_t>(data, pitch, width, height, round_a_, round_b_, four);
    width *= round_edges;
    height *= round_edges;
    pitch = static_cast<std::size_t>(width) * bpp;
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
  gl::glUniform1f(u_pixels_, background ? 0.0f : static_cast<float>(look.pixels));
  gl::glUniform1f(u_grid_, background ? 0.0f : look.grid);
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
  if (look.whole_pixels && (look.fill == FillMode::Fit || look.fill == FillMode::Ambient)) {
    // Every game pixel exactly k by k screen pixels, the largest k that fits: perfectly even and
    // unblended, with a little more border (the picture at 6x on a 1080p screen, 9x on 1440p).
    const float pw = (sx1 - sx0) * tw, ph = (sy1 - sy0) * th;
    const float k = std::max(1.0f, std::floor(std::min(static_cast<float>(out_w) / pw, static_cast<float>(out_h) / ph)));
    dw = pw * k;
    dh = ph * k;
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
  // Bicubic unless the frame is about twice the size it is shown at or more: then the mipmaps
  // average whole blocks of pixels (exactly 2x2 for supersampling). The sharp-pixel look keeps
  // nearest-neighbour sampling.
  bicubic_ = look.smooth && static_cast<float>(r.h) > 0.55f * (sy1 - sy0) * th ? 1.0f : 0.0f;
  gl::glUseProgram(program_);
  gl::glUniform1f(u_lines_, static_cast<float>(look.lines) * (sy1 - sy0) / v1);  // the console's lines, minus any crop
  gl::glUniform1f(u_scale_, static_cast<float>(r.h) / std::max(1.0f, (sy1 - sy0) * th));
  if (look.pixels != Look::Pixels::Plain) {
    // The pixel-art filters pick their own samples: texel centres (xBR) or edges (sharp pixels).
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }
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
