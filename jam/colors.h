#pragma once

#include "settings.h"

#define ICON_LENGTH 4

#define COMMAND_BORDER_SIZE 1

#define OFFSET_FROM_SCROLLBAR 2

enum color
  {
  editor_window = 1,
  command_window = 2,
  scroll_bar = 3,
  window_icon = 4,
  top_window = 5,
  column_window = 6,
  column_icon = 7,
  middle_drag = 8,
  right_drag = 9,
  modified_icon = 10,
  scroll_bar_2 = 11,
  selection = 12,
  selection_command = 13,
  highlight = 14,
  comment = 15,
  active_window = 16
  };


void init_colors(const settings& sett);