#pragma once

#include <vector>
#include <stdint.h>

enum screen_ex_type
  {
  SET_ICON,
  SET_SCROLLBAR,
  SET_TEXT
  };

struct screen_ex_pixel
  {
  screen_ex_pixel() : id(-1), type(-1), pos(-1) {}
  int64_t id; // refers to file_id
  int64_t pos; // refers to position in file
  int64_t type; // refers to type (could be scrollbar, or text, or ...)
  };

struct screen_ex
  {
  screen_ex(int ilines, int icols);

  int lines;
  int cols;
  std::vector<screen_ex_pixel> data;
  };

extern screen_ex pdc_ex;

void resize_term_ex(int ilines, int icols);
void add_ex(int64_t id, int64_t pos, int64_t type);
screen_ex_pixel get_ex(int row, int col);
void invalidate_range(int x, int y, int cols, int rows);

//#define FONT_WIDTH 11
//#define FONT_HEIGHT 20