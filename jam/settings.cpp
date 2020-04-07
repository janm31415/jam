#include "settings.h"

#include "pref_file.h"

#include <jam_filename.h>
#include <jam_exepath.h>

settings read_settings(const char* filename)
  {
  settings s;
  s.win_bg_red = 255;
  s.win_bg_green = 255;
  s.win_bg_blue = 240;
  s.tag_bg_red = 231;
  s.tag_bg_green = 251;
  s.tag_bg_blue = 252;
  s.scrb_red = 235;
  s.scrb_green = 233;
  s.scrb_blue = 162;
  s.text_red = 0;
  s.text_green = 0;
  s.text_blue = 0;
  s.tag_text_red = 0;
  s.tag_text_green = 0;
  s.tag_text_blue = 0;

  s.middle_red = 174;
  s.middle_green = 4;
  s.middle_blue = 17;

  s.right_red = 6;
  s.right_green = 98;
  s.right_blue = 3;

  s.selection_red = 235;
  s.selection_green = 233;
  s.selection_blue = 162;
  s.selection_tag_red = 158;
  s.selection_tag_green = 235;
  s.selection_tag_blue = 239;

  s.font_size = 17;
  s.font = JAM::get_folder(JAM::get_executable_path()) + "Font/DejaVuSansMono.ttf";

  s.command_text = "";
  s.column_text = "New Cut Paste Snarf Delcol";
  s.main_text = "Newcol Putall Exit";

  s.use_spaces_for_tab = true;
  s.tab_space = 2;

  pref_file f(filename, pref_file::READ);
  f["win_bg_red"] >> s.win_bg_red;
  f["win_bg_green"] >> s.win_bg_green;
  f["win_bg_blue"] >> s.win_bg_blue;
  f["tag_bg_red"] >> s.tag_bg_red;
  f["tag_bg_green"] >> s.tag_bg_green;
  f["tag_bg_blue"] >> s.tag_bg_blue;
  f["scrb_red"] >> s.scrb_red;
  f["scrb_green"] >> s.scrb_green;
  f["scrb_blue"] >> s.scrb_blue;
  f["text_red"] >> s.text_red;
  f["text_green"] >> s.text_green;
  f["text_blue"] >> s.text_blue;
  f["tag_text_red"] >> s.tag_text_red;
  f["tag_text_green"] >> s.tag_text_green;
  f["tag_text_blue"] >> s.tag_text_blue;
  f["middle_red"] >> s.middle_red;
  f["middle_green"] >> s.middle_green;
  f["middle_blue"] >> s.middle_blue;
  f["right_red"] >> s.right_red;
  f["right_green"] >> s.right_green;
  f["right_blue"] >> s.right_blue;

  f["selection_red"] >> s.selection_red;
  f["selection_green"] >> s.selection_green;
  f["selection_blue"] >> s.selection_blue;

  f["selection_tag_red"] >> s.selection_tag_red;
  f["selection_tag_green"] >> s.selection_tag_green;
  f["selection_tag_blue"] >> s.selection_tag_blue;

  f["font"] >> s.font;
  f["font_size"] >> s.font_size;

  f["use_spaces_for_tab"] >> s.use_spaces_for_tab;
  f["tab_space"] >> s.tab_space;

  return s;
  }

void write_settings(const settings& s, const char* filename)
  {
  pref_file f(filename, pref_file::WRITE);
  f << "win_bg_red" << s.win_bg_red;
  f << "win_bg_green" << s.win_bg_green;
  f << "win_bg_blue" << s.win_bg_blue;
  f << "tag_bg_red" << s.tag_bg_red;
  f << "tag_bg_green" << s.tag_bg_green;
  f << "tag_bg_blue" << s.tag_bg_blue;
  f << "scrb_red" << s.scrb_red;
  f << "scrb_green" << s.scrb_green;
  f << "scrb_blue" << s.scrb_blue;
  f << "text_red" << s.text_red;
  f << "text_green" << s.text_green;
  f << "text_blue" << s.text_blue;
  f << "tag_text_red" << s.tag_text_red;
  f << "tag_text_green" << s.tag_text_green;
  f << "tag_text_blue" << s.tag_text_blue;
  f << "middle_red" << s.middle_red;
  f << "middle_green" << s.middle_green;
  f << "middle_blue" << s.middle_blue;
  f << "right_red" << s.right_red;
  f << "right_green" << s.right_green;
  f << "right_blue" << s.right_blue;
  f << "selection_red" << s.selection_red;
  f << "selection_green" << s.selection_green;
  f << "selection_blue" << s.selection_blue;
  f << "selection_tag_red" << s.selection_tag_red;
  f << "selection_tag_green" << s.selection_tag_green;
  f << "selection_tag_blue" << s.selection_tag_blue;

  f << "font" << s.font;
  f << "font_size" << s.font_size;

  f << "use_spaces_for_tab" << s.use_spaces_for_tab;
  f << "tab_space" << s.tab_space;
  f.release();
  }