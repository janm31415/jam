#pragma once

#include <string>

struct settings
  {
  int win_bg_red, win_bg_green, win_bg_blue;
  int tag_bg_red, tag_bg_green, tag_bg_blue;
  int scrb_red, scrb_green, scrb_blue;
  int text_red, text_green, text_blue;
  int tag_text_red, tag_text_green, tag_text_blue;

  int middle_red, middle_green, middle_blue;
  int right_red, right_green, right_blue;

  int selection_red, selection_green, selection_blue;
  int selection_tag_red, selection_tag_green, selection_tag_blue;

  std::string command_text;
  std::string column_text;
  std::string main_text;

  bool use_spaces_for_tab;
  int tab_space;

  std::string font;
  int font_size;
  };

settings read_settings(const char* filename);

void write_settings(const settings& s, const char* filename);