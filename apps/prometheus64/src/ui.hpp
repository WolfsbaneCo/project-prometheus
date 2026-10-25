// Immediate-mode 2D drawing for the interface: rounded panels, gradients, images and text
// (Fira Sans, rasterised once into an atlas with stb_truetype). Coordinates are window pixels,
// top-left origin.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "gl.hpp"

namespace p64 {

struct Color {
  float r = 1, g = 1, b = 1, a = 1;
  Color with_alpha(float alpha) const { return {r, g, b, a * alpha}; }
};
Color rgb(uint32_t hex, float alpha = 1.0f);

enum class Align { Left, Center, Right };

class Ui {
 public:
  bool init(const std::filesystem::path& assets, std::string& error);
  void shutdown();

  void begin(int width, int height);
  void end();

  void rect(float x, float y, float w, float h, Color c, float radius = 0);
  void gradient(float x, float y, float w, float h, Color top, Color bottom, float radius = 0);
  void outline(float x, float y, float w, float h, Color c, float radius, float thickness);
  void image(GLuint texture, float x, float y, float w, float h, float alpha = 1.0f);
  // `y` is the top of the text line. Returns the width drawn.
  float text(float x, float y, const std::string& utf8, float size, Color c, bool bold = false,
             Align align = Align::Left);
  float measure(const std::string& utf8, float size, bool bold = false) const;
  // Shortens with an ellipsis to fit `max_width`.
  std::string fit(const std::string& utf8, float size, float max_width, bool bold = false) const;

  GLuint logo() const { return logo_tex_; }

 private:
  struct Glyph {
    float x0, y0, x1, y1;  // atlas pixels
    float xoff, yoff, xoff2, yoff2, advance;
  };
  struct Font {
    std::vector<Glyph> glyphs;  // code points 32..255
    float px = 0, ascent = 0;
  };
  struct Vertex {
    float x, y, u, v, r, g, b, a, mode, lx, ly, w, h, radius, thickness;
  };
  bool bake(const std::filesystem::path& file, Font& font, int atlas_y, std::vector<uint8_t>& atlas, std::string& error);
  void quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color top, Color bottom,
            float mode, float radius, float thickness);
  void flush();
  void use_texture(GLuint tex);

  Font regular_, bold_;
  GLuint atlas_tex_ = 0, logo_tex_ = 0, program_ = 0, vao_ = 0, vbo_ = 0, bound_tex_ = 0;
  GLint u_screen_ = -1;
  int width_ = 0, height_ = 0;
  std::vector<Vertex> batch_;
};

}  // namespace p64
