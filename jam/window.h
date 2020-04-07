#pragma once

#include <string>
#include <vector>
#include <stdint.h>

#include <jamlib/jam.h>

#include "settings.h"
#include "grid.h"

struct window
  {
  window(int x, int y, int cols, int rows, uint32_t fid, uint32_t nid, bool command_window);
  ~window();

  void resize(int cols, int rows);
  void move(int x, int y);

  void kill_pipe();
    
  int outer_x, outer_y, outer_cols, outer_rows;
  int x, y, cols, rows;
  uint32_t file_id, nephew_id;
  bool is_command_window;  
  int64_t file_pos, file_col, wordwrap_row;
  int64_t previous_file_pos; // this is a history item for computing the multiline comment. We need to know whether the start of our window is inside or outside comment. This is a global problem, and thus slow. Therefore we save some data we already computed.
  bool previous_file_pos_was_comment; // Idem as previous_file_pos.
  bool word_wrap;
  double scroll_fraction;

  bool piped;
  bool highlight_comments;
  std::wstring piped_prompt;
  uint32_t piped_prompt_index;
  std::vector<std::string> piped_prompt_history;
#ifdef _WIN32
  void* process;
#else
  int process[3];
#endif
  };

//void draw(window w, jamlib::app_state state, const settings& sett);

jamlib::range get_window_range(window w, jamlib::app_state state);

struct window_pair
  {
  window_pair(int x, int y, int cols, int rows, uint32_t wid, uint32_t cwid);
  ~window_pair();


  void resize(int cols, int rows);
  void move(int x, int y);

  uint32_t window_id, command_window_id;

  int x, y, cols, rows;
  };

std::vector<window> draw(const grid& g, const std::vector<window_pair>& window_pairs, std::vector<window> windows, jamlib::app_state state, const settings& sett);


void save_window_to_stream(std::ostream& str, const window& w);

window load_window_from_stream(std::istream& str);

void save_window_pair_to_stream(std::ostream& str, const window_pair& w);

window_pair load_window_pair_from_stream(std::istream& str);