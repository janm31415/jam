#pragma once

#include <stdint.h>
#include "pdcex.h"

struct rearranging_window_data
  {
  int64_t rearranging_file_id;
  unsigned long icon_sign;
  unsigned long current_sign_left, current_sign_mid, current_sign_right;
  int x, y;
  };

struct mouse_data
  {
  mouse_data();
  bool left_dragging;
  bool right_dragging;
  bool middle_dragging;
  int32_t mouse_x;
  int32_t mouse_y;
  int32_t prev_mouse_x;
  int32_t prev_mouse_y;
  int32_t mouse_x_at_button_press;
  int32_t mouse_y_at_button_press;
  double wheel_rotation;
  bool left_button_down;
  bool right_button_down;
  bool middle_button_down;
  bool rearranging_windows;
  screen_ex_pixel left_drag_start;
  screen_ex_pixel left_drag_end;
  screen_ex_pixel middle_drag_start;
  screen_ex_pixel middle_drag_end;
  screen_ex_pixel right_drag_start;
  screen_ex_pixel right_drag_end;
  rearranging_window_data rwd;
  };

extern mouse_data mouse;