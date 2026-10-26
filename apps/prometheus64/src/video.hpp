// Takes the core's frame (rendered by the core into our framebuffer, or handed over as pixels)
// and presents it: fill mode, overscan crop, sharpening, scanlines and a smooth downscale.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "gl.hpp"
#include "libretro.h"

namespace p64 {

enum class FillMode { Fit, SmartZoom, Widescreen, Ambient, Stretch, Count };
const char* fill_mode_name(FillMode m);

struct Look {
  FillMode fill = FillMode::Fit;
  float aspect = 4.0f / 3.0f;  // display shape of the game picture
  float overscan = 0.0f;       // fraction cropped from each edge
  float sharpen = 0.0f;        // 0 = off
  float scanlines = 0.0f;      // 0 = off
  bool smooth = true;          // false = sharp pixels (the "Native" look)
};

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

class Video {
 public:
  bool init(std::string& error);
  void shutdown();

  // The framebuffer the core renders into when it uses hardware rendering.
  void prepare_target(unsigned width, unsigned height);
  uintptr_t target_framebuffer() const { return target_fbo_; }

  void hardware_frame(unsigned width, unsigned height);
  void software_frame(const void* data, unsigned width, unsigned height, std::size_t pitch, retro_pixel_format format);
  void clear_frame() { frame_w_ = frame_h_ = 0; }
  bool has_frame() const { return frame_w_ > 0; }

  // Puts GL state back to what the presenter expects (the core shares the context).
  void begin_present(int out_w, int out_h);
  // Draws the game picture; returns where it went, in window pixels (top-left origin).
  Rect draw_game(int out_w, int out_h, const Look& look, float alpha = 1.0f);

  // Reads the window back as RGB (bottom-up rows), for screenshots and tests.
  bool read_pixels(int w, int h, uint8_t* rgb);

 private:
  GLuint compile(GLenum type, const char* src, std::string& error);
  void draw_quad(const float src[4], const float dst[4], bool background, float lod, float dim, const Look& look,
                 float step_x, float step_y, float alpha);

  GLuint program_ = 0, vao_ = 0;
  GLint u_bicubic_ = -1, u_size_ = -1;
  float bicubic_ = 0.0f, size_w_ = 1.0f, size_h_ = 1.0f;
  GLint u_src_ = -1, u_dst_ = -1, u_mode_ = -1, u_lod_ = -1, u_dim_ = -1, u_step_ = -1, u_sharpen_ = -1,
        u_scan_ = -1, u_lines_ = -1, u_alpha_ = -1;
  GLuint target_fbo_ = 0, target_tex_ = 0, target_depth_ = 0;
  unsigned target_w_ = 0, target_h_ = 0;
  GLuint sw_tex_ = 0;
  unsigned sw_w_ = 0, sw_h_ = 0;
  bool frame_is_hw_ = false;
  unsigned frame_w_ = 0, frame_h_ = 0;
};

}  // namespace p64
