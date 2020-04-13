#pragma once

#include "settings.h"
#include "window.h"
#include "grid.h"
#include "async_messages.h"

#include <jamlib/jam.h>
#include <vector>


struct app_state
  {
  int32_t w, h;
  std::vector<window> windows;
  std::vector<uint32_t> file_id_to_window_id;
  std::vector<window_pair> window_pairs;
  grid g;
  jamlib::app_state file_state;  
  jamlib::buffer snarf_buffer;
  jamlib::buffer find_buffer;
  };

struct engine
  {
  app_state state;
  settings sett;
  async_messages messages;

  engine(int w, int h, int argc, char** argv, const settings& s);
  ~engine();

  void run();

  };