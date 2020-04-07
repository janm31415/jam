#include "engine.h"
#include "clipboard.h"
#include "colors.h"
#include "pdcex.h"
#include "mouse.h"
#include "keyboard.h"
#include "utils.h"
#include "serialize.h"
#include <jam_active_folder.h>
#include <jam_file_utils.h>
#include <jam_exepath.h>

#include <SDL.h>
#include <curses.h>
#include <sstream>

#include <thread>
#include <cassert>

#include <map>
#include <functional>

#include <jam_pipe.h>
#include <jam_encoding.h>
#include <jam_filename.h>

extern "C"
  {
#include <sdl2/pdcsdl.h>
  }

namespace
  {
  int font_width, font_height;
  settings* gp_settings = nullptr;
  //SDL_Cursor* gp_cursor;
  }

/*
// XPM
static const char *arrow[] = {
  // width height num_colors chars_per_pixel
  "    32    32        3            1",
  // colors
  "X c #000000",
  ". c #ffffff",
  "  c None",
  // pixels
  "X                               ",
  "XX                              ",
  "X.X                             ",
  "X..X                            ",
  "X...X                           ",
  "X....X                          ",
  "X.....X                         ",
  "X......X                        ",
  "X.......X                       ",
  "X........X                      ",
  "X.....XXXXX                     ",
  "X..X..X                         ",
  "X.X X..X                        ",
  "XX  X..X                        ",
  "X    X..X                       ",
  "     X..X                       ",
  "      X..X                      ",
  "      X..X                      ",
  "       XX                       ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "                                ",
  "0,0"
  };

static SDL_Cursor *init_system_cursor(const char *image[])
  {
  int i, row, col;
  Uint8 data[4 * 32];
  Uint8 mask[4 * 32];
  int hot_x, hot_y;

  i = -1;
  for (row = 0; row < 32; ++row) {
    for (col = 0; col < 32; ++col) {
      if (col % 8) {
        data[i] <<= 1;
        mask[i] <<= 1;
        }
      else {
        ++i;
        data[i] = mask[i] = 0;
        }
      switch (image[4 + row][col]) {
        case 'X':
          data[i] |= 0x01;
          mask[i] |= 0x01;
          break;
        case '.':
          mask[i] |= 0x01;
          break;
        case ' ':
          break;
        }
      }
    }
  sscanf(image[4 + row], "%d,%d", &hot_x, &hot_y);
  return SDL_CreateCursor(data, mask, 32, 32, hot_x, hot_y);
  }
  */

std::string cleanup_foldername(std::string foldername)
  {
  if (foldername.back() == '\\')
    foldername.back() = '/';
  if (foldername.back() != '/')
    foldername.push_back('/');
  return foldername;
  }
std::string get_utf8_string(typename jamlib::buffer::iterator first, typename jamlib::buffer::iterator last, jamlib::encoding enc)
  {
  std::string str;
  str.reserve(std::distance(first, last));
  switch (enc)
    {
    case jamlib::ENC_UTF8:
    {
    utf8::utf16to8(first, last, std::back_inserter(str));
    break;
    }
    case jamlib::ENC_ASCII:
    {
    for (; first != last; ++first)
      str.push_back((char)*first);
    }
    }
  return str;
  }

int get_cols()
  {
  return SP->cols;
  }

int get_lines()
  {
  return SP->lines - 1;
  }

int get_y_offset_from_top(const column& c, int number_of_column_cols, const app_state& state)
  {
  uint32_t top_length = state.file_state.files[state.windows[state.g.topline_window_id].file_id].content.size();
  uint32_t top_rows = get_cols() > ICON_LENGTH + 1 ? top_length / (get_cols() - 1 - ICON_LENGTH) + 1 + COMMAND_BORDER_SIZE : 0;

  uint32_t column_command_length = state.file_state.files[state.windows[c.column_command_window_id].file_id].content.size();
  uint32_t column_command_rows = number_of_column_cols > ICON_LENGTH + 1 ? column_command_length / (number_of_column_cols - 1 - ICON_LENGTH) + 1 + COMMAND_BORDER_SIZE : 0;

  return (int)(top_rows + column_command_rows);
  }

int get_minimum_number_of_rows(const column_item& ci, int number_of_column_cols, const app_state& state)
  {
  uint32_t file_id = state.windows[state.window_pairs[ci.window_pair_id].command_window_id].file_id;
  uint32_t tag_length = state.file_state.files[file_id].content.size();
  uint32_t tag_rows = get_cols() > ICON_LENGTH + 1 ? tag_length / (number_of_column_cols - 1 - ICON_LENGTH) + 1 + COMMAND_BORDER_SIZE : 0;
  return (int)tag_rows;
  }

int get_available_rows(const column& c, int number_of_column_cols, const app_state& state)
  {
  int available_rows = get_lines() - get_y_offset_from_top(c, number_of_column_cols, state);
  return available_rows;
  }

int64_t find_window_pair_id(int x, int y, const app_state& state)
  {
  int icols = get_cols();
  for (auto& c : state.g.columns)
    {
    int left = (int)(c.left*icols);
    int right = (int)(c.right*icols);
    if (x >= left && x < right)
      {
      y -= get_y_offset_from_top(c, right - left, state);
      int irows = get_available_rows(c, right - left, state);
      for (auto& ci : c.items)
        {
        int top = (int)(irows*ci.top_layer);
        int bottom = (int)(irows*ci.bottom_layer);
        if (y >= top && y <= bottom)
          return ci.window_pair_id;
        }
      }
    }
  return -1;
  }

double get_scroll_fraction(int x, int y, int64_t file_id, const app_state& state)
  {
  const auto& w = state.windows[state.file_id_to_window_id[file_id]];
  if (y <= w.y)
    return 0.0;
  if (y >= w.y + w.rows)
    return 1.0;
  double fract = (double)(y - w.y) / (double)w.rows;
  return fract;
  }

app_state jump_to_pos(int64_t pos, uint32_t file_id, app_state state)
  {
  if (pos < 0)
    return state;

  const auto& f = state.file_state.files[file_id];

  if (pos > f.content.size())
    pos = f.content.size();

  auto& w = state.windows[state.file_id_to_window_id[file_id]];

  int64_t begin_of_line_pos = pos;
  auto it = f.content.rbegin() + (f.content.size() - pos);
  auto it_end = f.content.rend();
  for (; it != it_end; ++it, --begin_of_line_pos)
    {
    if (*it == '\n')
      break;
    }
  w.file_pos = begin_of_line_pos;
  w.wordwrap_row = (pos - begin_of_line_pos) / (w.cols - 1);
  return state;
  }

app_state resize(app_state state)
  {
  int icols = get_cols();
  int irows = get_lines();

  uint32_t top_length = state.file_state.files[state.windows[state.g.topline_window_id].file_id].content.size();
  uint32_t top_rows = icols > ICON_LENGTH + 1 ? top_length / (icols - 1 - ICON_LENGTH) + 1 + COMMAND_BORDER_SIZE : 0;

  state.windows[state.g.topline_window_id].move(0, 0);
  state.windows[state.g.topline_window_id].resize(icols, top_rows);

  for (auto& c : state.g.columns)
    {
    int left = (int)(c.left*icols);
    int right = (int)(c.right*icols);

    uint32_t column_command_length = state.file_state.files[state.windows[c.column_command_window_id].file_id].content.size();

    uint32_t column_command_rows = right - left > ICON_LENGTH + 1 ? column_command_length / (right - left - 1 - ICON_LENGTH) + 1 + COMMAND_BORDER_SIZE : 0;

    state.windows[c.column_command_window_id].move(left, top_rows);
    state.windows[c.column_command_window_id].resize(right - left, column_command_rows);

    int available_rows = irows - top_rows - column_command_rows;


    for (auto& ci : c.items)
      {
      int y = top_rows + column_command_rows + (int)std::round(available_rows*ci.top_layer);
      int y2 = top_rows + column_command_rows + (int)std::round(available_rows*ci.bottom_layer);
      int x = left;
      int h = y2 - y;
      int w = right - left;
      state.window_pairs[ci.window_pair_id].move(x, y);
      state.window_pairs[ci.window_pair_id].resize(w, h);
      }
    }

  return state;
  }

app_state resize_font(app_state state, int font_size)
  {
  pdc_font_size = font_size;
  gp_settings->font_size = font_size;
  TTF_CloseFont(pdc_ttffont);
  //pdc_ttffont = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", pdc_font_size);
  //pdc_ttffont = TTF_OpenFont("D:/_Development/GameDev/Build/games/jamterm/Consolas-Braille-Mono.ttf", pdc_font_size);
  //pdc_ttffont = TTF_OpenFont("D:/_Development/fonts/SourceCodePro-Regular.ttf", pdc_font_size);
  //pdc_ttffont = TTF_OpenFont("D:/_Development/fonts/unifont-12.1.04.ttf", pdc_font_size);
  //pdc_ttffont = TTF_OpenFont("D:/_Development/fonts/freefont-20120503/freemono.ttf", pdc_font_size);

  //pdc_ttffont = TTF_OpenFont("D:/_Development/fonts/DejaVuSansMono.ttf", pdc_font_size);

  pdc_ttffont = TTF_OpenFont(gp_settings->font.c_str(), pdc_font_size);

  //pdc_ttffont = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", pdc_font_size);

  if (!pdc_ttffont)
    pdc_ttffont = TTF_OpenFont("C:/Windows/Fonts/consola.ttf", pdc_font_size);

  TTF_SizeText(pdc_ttffont, "W", &font_width, &font_height);
  stdscr->_clear = TRUE;
  SDL_GetWindowSize(pdc_window, &state.w, &state.h);
  pdc_fheight = font_height;
  pdc_fwidth = font_width;
  pdc_fthick = pdc_font_size / 20 + 1;

  resize_term(state.h / font_height, state.w / font_width);
  resize_term_ex(state.h / font_height, state.w / font_width);

  return resize(state);
  }

jamlib::buffer get_folder_list(const std::string& foldername);
std::optional<app_state> new_column_command(app_state state, int64_t id, const std::string&);
std::optional<app_state> load_file(std::string filename, app_state state, int64_t id);
std::optional<app_state> load_folder(std::string folder, app_state state, int64_t id);
app_state make_window_piped(app_state state, int64_t file_id, std::string win_command);
bool has_valid_file_pos(const window& w, const app_state& state);
app_state update_command_text(app_state state, uint32_t command_id);
std::optional<app_state> optimize_column(app_state state, int64_t id);


app_state set_fileids(app_state state)
  {
  uint64_t id = 0;
  for (auto& f : state.file_state.files)
    {
    f.file_id = id++;
    }
  return state;
  }

app_state check_tags_at_startup(app_state state)
  {
  // this method checks whether the top tag contains the standard text gp_settings->main_text
  // and the column tags contain the standard text gp_settings->column_text
  // if this text is not present, then we add it to the front
  auto active_f = state.file_state.active_file;
  window& topw = state.windows[state.g.topline_window_id];
  state.file_state.active_file = topw.file_id;
  std::stringstream str;
  str << "0+/(" << resolve_jamlib_escape_characters(gp_settings->main_text) << ")/";
  auto state_tmp = *jamlib::handle_command(state.file_state, str.str());
  if (state_tmp.files[state_tmp.active_file].dot.r.p1 == state_tmp.files[state_tmp.active_file].dot.r.p2) // not found
    {
    std::stringstream ss2;
    ss2 << "i/" << gp_settings->main_text << " /";
    state.file_state.files[state.file_state.active_file].dot.r.p1 = state.file_state.files[state.file_state.active_file].dot.r.p2 = 0;
    state.file_state = *jamlib::handle_command(state.file_state, ss2.str());
    state.file_state.files[state.file_state.active_file].dot.r.p1 = state.file_state.files[state.file_state.active_file].dot.r.p2 = state.file_state.files[state.file_state.active_file].content.size();
    // remove undo redo history
    state.file_state.files[state.file_state.active_file].modification_mask = 0;
    state.file_state.files[state.file_state.active_file].history = immutable::vector<jamlib::snapshot, false>();
    state.file_state.files[state.file_state.active_file].undo_redo_index = 0;
    }

  size_t nr_of_columns = state.g.columns.size();
  for (size_t c = 0; c < nr_of_columns; ++c)
    {
    window& column_win = state.windows[state.g.columns[c].column_command_window_id];
    state.file_state.active_file = column_win.file_id;
    std::stringstream ss;
    str << "0+/(" << resolve_jamlib_escape_characters(gp_settings->column_text) << ")/";
    auto state_tmp = *jamlib::handle_command(state.file_state, str.str());
    if (state_tmp.files[state_tmp.active_file].dot.r.p1 == state_tmp.files[state_tmp.active_file].dot.r.p2) // not found
      {
      std::stringstream ss2;
      ss2 << "i/" << gp_settings->column_text << " /";
      state.file_state.files[state.file_state.active_file].dot.r.p1 = state.file_state.files[state.file_state.active_file].dot.r.p2 = 0;
      state.file_state = *jamlib::handle_command(state.file_state, ss2.str());
      state.file_state.files[state.file_state.active_file].dot.r.p1 = state.file_state.files[state.file_state.active_file].dot.r.p2 = state.file_state.files[state.file_state.active_file].content.size();
      // remove undo redo history
      state.file_state.files[state.file_state.active_file].modification_mask = 0;
      state.file_state.files[state.file_state.active_file].history = immutable::vector<jamlib::snapshot, false>();
      state.file_state.files[state.file_state.active_file].undo_redo_index = 0;
      }
    }

  size_t nr_of_windows = state.windows.size();
  for (size_t i = 0; i < nr_of_windows; ++i)
    {
    auto& w = state.windows[i];
    if (w.is_command_window && w.nephew_id != (uint32_t)-1)
      state = update_command_text(state, w.file_id);
    }

  state.file_state.active_file = active_f;
  return state;
  }

engine::engine(int w, int h, int argc, char** argv, const settings& s) : sett(s)
  {
  //gp_cursor = init_system_cursor(arrow);
  //SDL_SetCursor(gp_cursor);

  TTF_SizeText(pdc_ttffont, "W", &font_width, &font_height);
  gp_settings = &sett;
  start_color();
  use_default_colors();
  nodelay(stdscr, TRUE);
  noecho();
  init_colors(sett);
  bkgd(COLOR_PAIR(editor_window));
  state.w = w;
  state.h = h;
  int maxrow, maxcol;
  getmaxyx(stdscr, maxrow, maxcol);

  // load previously saved state
  if (argc < 2)
    state = load_from_file(get_file_in_executable_path("temp.txt"));
  uint32_t sz = (uint32_t)state.windows.size();
  auto active_file = state.file_state.active_file;
  for (uint32_t j = 0; j < sz; ++j)
    {
    if (!state.windows[j].is_command_window && !state.windows[j].piped) // load files or folders
      {
      uint32_t file_id = state.windows[j].file_id;
      std::string filename = remove_quotes_from_path(state.file_state.files[file_id].filename);
      if (JAM::file_exists(filename))
        {
        std::stringstream load_file_command;
        load_file_command << "r " << state.file_state.files[file_id].filename;
        auto d = state.file_state.files[file_id].dot;
        state.file_state.active_file = file_id;
        state.file_state = *jamlib::handle_command(state.file_state, load_file_command.str());
        state.file_state.files[file_id].modification_mask = 0;
        state.file_state.files[file_id].history = immutable::vector<jamlib::snapshot, false>();
        state.file_state.files[file_id].undo_redo_index = 0;
        if (d.r.p1 > state.file_state.files[file_id].content.size())
          d.r.p1 = state.file_state.files[file_id].content.size();
        if (d.r.p2 > state.file_state.files[file_id].content.size())
          d.r.p2 = state.file_state.files[file_id].content.size();
        state.file_state.files[file_id].dot = d;
        if (state.windows[j].file_pos > state.file_state.files[file_id].content.size())
          state.windows[j].file_pos = 0;

        }
      else if (JAM::is_directory(filename))
        {
        state.file_state.files[file_id].content = get_folder_list(filename);
        auto d = state.file_state.files[file_id].dot;
        if (d.r.p1 > state.file_state.files[file_id].content.size())
          d.r.p1 = state.file_state.files[file_id].content.size();
        if (d.r.p2 > state.file_state.files[file_id].content.size())
          d.r.p2 = state.file_state.files[file_id].content.size();
        state.file_state.files[file_id].dot = d;
        if (state.windows[j].file_pos > state.file_state.files[file_id].content.size())
          state.windows[j].file_pos = 0;
        }

      if (!has_valid_file_pos(state.windows[j], state))
        state.windows[j].file_pos = 0;

      }
    if (state.windows[j].piped)
      {
      uint32_t file_id = state.windows[j].file_id;
      std::string filename = state.file_state.files[file_id].filename;
      state = make_window_piped(state, file_id, filename.substr(1));
      }
    }
  state.file_state.active_file = active_file;
  for (auto& f : state.file_state.files)
    {
    if (f.dot.r.p1 > f.content.size() || f.dot.r.p2 > f.content.size())
      {
      f.dot.r.p1 = 0;
      f.dot.r.p2 = 0;
      }
    }

  if (state.file_state.files.empty()) // if temp.txt was an invalid file then initialise
    {
    state.file_state.active_file = 0;
    state.file_state.files.emplace_back();
    auto command_window = state.file_state.files.back().content.transient();
    std::string text = s.main_text;
    for (auto ch : text)
      command_window.push_back(ch);
    state.file_state.files.back().content = command_window.persistent();
    state.file_state.files.back().dot.r.p1 = command_window.size();
    state.file_state.files.back().dot.r.p2 = command_window.size();
    state.file_id_to_window_id.push_back((uint32_t)state.windows.size());
    state.windows.emplace_back(0, 0, maxcol, 1, (uint32_t)state.file_state.files.size() - 1, (uint32_t)-1, true);
    state.g.topline_window_id = (uint32_t)state.windows.size() - 1;

    state = *new_column_command(state, 0, "");
    }

  for (int j = 1; j < argc; ++j) // open any files/folders that were given as argument, if not yet opened
    {
    std::string filename = (argv[j]);
    if (JAM::is_directory(filename))
      filename = cleanup_foldername(filename);
    bool already_open = false;
    for (const auto& f : state.file_state.files)
      {
      if (f.filename == filename)
        {
        already_open = true;
        break;
        }
      }
    if (already_open)
      continue;
    if (JAM::file_exists(filename))
      {
      if (state.g.columns.empty() || state.g.columns.front().items.empty())
        state = *load_file(filename, state, -1);
      else
        {
        //set active file to the last item in the first column. The new window will be created below this one.
        state.file_state.active_file = state.windows[state.window_pairs[state.g.columns.front().items.back().window_pair_id].window_id].file_id;
        state = *load_file(filename, state, state.file_state.active_file);
        }
      }
    else if (JAM::is_directory(filename))
      {
      state = *load_folder(filename, state, -1);
      }
    }

  state = check_tags_at_startup(state);
  state = set_fileids(state);

  state.w = w;
  state.h = h;

  SDL_ShowCursor(1);
  SDL_SetWindowSize(pdc_window, w, h);

  SDL_DisplayMode DM;
  SDL_GetCurrentDisplayMode(0, &DM);

  SDL_SetWindowPosition(pdc_window, (DM.w - w) / 2, (DM.h - h) / 2);

  resize_term(h / font_height, w / font_width);
  resize_term_ex(h / font_height, w / font_width);

  SDL_Rect dest;

  dest.x = 0;
  dest.y = 0;
  dest.w = state.w;
  dest.h = state.h;


  SDL_FillRect(pdc_screen, &dest, SDL_MapRGB(pdc_screen->format, (uint8_t)sett.win_bg_red, (uint8_t)sett.win_bg_green, (uint8_t)sett.win_bg_blue));

  //Reloading the font for aliasing issues
  state = resize_font(state, sett.font_size);
  }

engine::~engine()
  {
  save_to_file(get_file_in_executable_path("temp.txt"), state);
  for (auto& w : state.windows)
    w.kill_pipe();
  //SDL_FreeCursor(gp_cursor);
  }

bool this_is_a_command_window(const app_state& state, uint32_t command_id)
  {
  const auto& w = state.windows[state.file_id_to_window_id[command_id]];
  if (!w.is_command_window)
    return false;
  if (w.nephew_id == (uint32_t)-1)
    return false;
  return true;
  }

jamlib::address find_command_window_fixed_dot(const app_state& state, uint32_t command_id)
  {
  assert(this_is_a_command_window(state, command_id));
  jamlib::address out;
  const auto& f = state.file_state.files[command_id];
  out.file_id = command_id;
  out.r.p1 = f.content.size();
  out.r.p2 = f.content.size();
  const wchar_t bar = (wchar_t)'|';
  auto it0 = std::find(f.content.begin(), f.content.end(), bar); // find first bar
  if (it0 == f.content.end())
    return out;
  auto it1 = std::find(it0 + 1, f.content.end(), bar); // find second bar
  out.r.p1 = it0 - f.content.begin();
  out.r.p2 = it1 == f.content.end() ? f.content.size() : it1 - f.content.begin();
  return out;
  }

std::wstring make_command_text(const app_state& state, uint32_t command_id)
  {
  const auto& f = state.file_state.files[command_id];
  std::stringstream out;
  out << f.filename << " | Del | " << gp_settings->command_text;
  return JAM::convert_string_to_wstring(out.str());
  }

std::string get_command_text(app_state state, uint32_t command_id)
  {
  assert(this_is_a_command_window(state, command_id));
  auto& w = state.windows[state.file_id_to_window_id[command_id]];

  auto& f = state.file_state.files[command_id];
  auto& textf = state.file_state.files[w.nephew_id];

  bool put = is_modified(textf);//textf.modification_mask & 1 == 1;
  bool undo = !textf.history.empty() && textf.undo_redo_index > 0;
  bool redo = !textf.history.empty() && textf.undo_redo_index < textf.history.size();

  bool get = JAM::is_directory(f.filename);
  //bool is_dir = 
  std::stringstream str;
  str << " Del";
  if (get)
    str << " Get";
  if (undo)
    str << " Undo";
  if (redo)
    str << " Redo";
  if (put)
    str << " Put";
  str << " ";
  return str.str();
  }

bool should_update_command_text(app_state state, uint32_t command_id)
  {
  assert(this_is_a_command_window(state, command_id));
  auto text = get_command_text(state, command_id);
  auto dot = find_command_window_fixed_dot(state, command_id);
  state.file_state.active_file = command_id;
  std::stringstream ss;
  ss << "#" << dot.r.p1 + 1 << ",#" << dot.r.p2 << " /" << text << "/";
  state.file_state = *jamlib::handle_command(state.file_state, ss.str());
  auto found_dot = state.file_state.files[state.file_state.active_file].dot;
  if (dot.r.p1 + 1 == found_dot.r.p1 && dot.r.p2 == found_dot.r.p2)
    return false;
  return true;
  }

app_state update_command_text(app_state state, uint32_t command_id)
  {
  assert(this_is_a_command_window(state, command_id));
  auto text = get_command_text(state, command_id);
  auto dot = find_command_window_fixed_dot(state, command_id);
  std::stringstream ss;
  ss << "#" << dot.r.p1 + 1 << ",#" << dot.r.p2 << " c/" << text << "/";
  auto af = state.file_state.active_file;
  auto dot_save = state.file_state.files[command_id].dot;
  int64_t sz0 = (int64_t)state.file_state.files[command_id].content.size();
  state.file_state.active_file = command_id;
  state.file_state = *jamlib::handle_command(state.file_state, ss.str());
  int64_t sz1 = (int64_t)state.file_state.files[command_id].content.size();
  if (dot_save.r.p1 >= dot.r.p2)
    dot_save.r.p1 += (sz1 - sz0);
  if (dot_save.r.p2 >= dot.r.p2)
    dot_save.r.p2 += (sz1 - sz0);
  state.file_state.files[command_id].dot = dot_save;
  state.file_state.active_file = af;
  return state;
  }

bool has_valid_file_pos(const window& w, const app_state& state)
  {
  const auto pos = w.file_pos;
  if (!pos)
    return true;
  const auto& buf = state.file_state.files[w.file_id].content;
  if (pos > buf.size())
    return false;
  auto ch = buf[pos - 1];
  return (ch == '\n');
  }

bool has_valid_file_pos(const app_state& state)
  {
  return has_valid_file_pos(state.windows[state.file_id_to_window_id[state.file_state.active_file]], state);
  }

int64_t get_line_length(jamlib::file f, int64_t pos);

int64_t get_begin_of_line(jamlib::file f)
  {
  int64_t pos = f.dot.r.p1;
  auto it = f.content.rbegin() + (f.content.size() - pos);
  auto it_end = f.content.rend();
  for (; it != it_end; ++it, --pos)
    {
    if (*it == '\n')
      return pos;
    }
  return 0;
  }

int64_t get_end_of_line(jamlib::file f)
  {
  int64_t pos = f.dot.r.p1;
  auto it = f.content.begin() + pos;
  auto it_end = f.content.end();
  if (it == it_end)
    return pos;
  for (; it != it_end; ++it, ++pos)
    {
    if (*it == '\n')
      return pos;
    }
  if (pos == f.content.size())
    return pos;
  return pos - 1;
  }

int64_t get_line_begin(jamlib::file f, int64_t pos)
  {
  if (pos > f.content.size())
    pos = f.content.size();
  f.dot.r.p1 = pos;
  f.dot.r.p2 = pos;
  int64_t b = get_begin_of_line(f);
  return b;
  }

int64_t get_line_end(jamlib::file f, int64_t pos)
  {
  if (pos > f.content.size())
    pos = f.content.size();
  f.dot.r.p1 = pos;
  f.dot.r.p2 = pos;
  int64_t e = get_end_of_line(f);
  return e;
  }

bool on_last_line(jamlib::file f, int64_t pos)
  {
  if (pos > f.content.size())
    return true;
  pos = get_line_end(f, pos);
  if (pos < f.content.size())
    return false;
  return true;
  }

app_state draw(app_state state, const settings& sett)
  {
  erase();

  state.windows = draw(state.g, state.window_pairs, state.windows, state.file_state, sett);

  curs_set(0);
  refresh();

  return state;
  }

app_state check_boundaries(app_state state, bool word_wrap)
  {
  assert(has_valid_file_pos(state));
  auto& f = state.file_state.files[state.file_state.active_file];

  if (state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_col < 0)
    state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_col = 0;
  if (state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos < 0)
    state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos = 0;

  if (word_wrap)
    {
    state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_col = 0;
    auto r = get_window_range(state.windows[state.file_id_to_window_id[state.file_state.active_file]], state.file_state);

    /*
    while (f.dot.r.p1 >= r.p2 && f.dot.r.p1 < f.content.size())
      {
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row = 0;
      auto le = get_line_length(f, state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos);
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos += le+1;
      r = get_window_range(state.windows[state.file_id_to_window_id[state.file_state.active_file]], state.file_state);
      }
      */


    while (f.dot.r.p1 >= r.p2 && f.dot.r.p1 < f.content.size())
      {
      auto it = f.content.begin() + r.p2;
      auto it_end = f.content.end();
      bool last_line = false;
      for (; it != it_end; ++it, ++r.p2)
        {
        if (f.dot.r.p1 <= r.p2)
          last_line = true;
        if (*it == '\n')
          {
          auto le = get_line_length(f, state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos);
          state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos += le + 1;
          assert(has_valid_file_pos(state));
          if (last_line)
            break;
          }
        }
      if (it == it_end)
        {
        auto le = get_line_length(f, state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos);
        state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos += le + 1;
        assert(has_valid_file_pos(state));
        }

      state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row = 0;

      r = get_window_range(state.windows[state.file_id_to_window_id[state.file_state.active_file]], state.file_state);
      assert(r.p2 > f.dot.r.p1);
      }

    /*
    while (f.dot.r.p1 < r.p1)
      {
      auto old_r = r;
      auto le = get_line_length(f, state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos - 1);
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos -= le+1;
      r = get_window_range(state.windows[state.file_id_to_window_id[state.file_state.active_file]], state.file_state);
      int64_t line_length = old_r.p1 - r.p1;
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row = line_length / (state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1);
      }
      */

    while (f.dot.r.p1 < r.p1)
      {
      auto it = f.content.rbegin() + (f.content.size() - r.p1);
      auto it_end = f.content.rend();
      int64_t last_line_pos = r.p1;

      for (; it != it_end; ++it, --r.p1)
        {
        if (f.dot.r.p1 >= r.p1)
          break;
        if (*it == '\n')
          {
          last_line_pos = r.p1;
          auto le = get_line_length(f, state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos - 1);
          state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos -= le + 1;
          assert(has_valid_file_pos(state));
          }
        }
      while (it != it_end && *it != '\n')
        {
        ++it;
        --r.p1;
        }
      int64_t line_length = last_line_pos - r.p1;
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row = line_length / (state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1);
      r = get_window_range(state.windows[state.file_id_to_window_id[state.file_state.active_file]], state.file_state);
      assert(r.p1 <= f.dot.r.p1);
      }

    auto it = f.content.begin() + r.p1;
    auto it_end = f.content.begin() + f.dot.r.p1;
    int64_t row = 0;
    int64_t col = 0;
    for (; it != it_end; ++it)
      {
      if (*it == '\n')
        {
        col = 0;
        ++row;
        }
      else
        {
        ++col;
        if (col >= state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1)
          {
          col = 0;
          ++row;
          }
        }
      }
    if (row - state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row >= state.windows[state.file_id_to_window_id[state.file_state.active_file]].rows)
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row = row - state.windows[state.file_id_to_window_id[state.file_state.active_file]].rows + 1;
    if (row - state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row < 0)
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row = row;

    }
  else
    {
    state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row = 0;
    int64_t b = get_begin_of_line(f);
    int64_t e = get_end_of_line(f);
    int64_t pos = b + state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_col;
    if (pos > e)
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_col = f.dot.r.p1 - b;
    else if (f.dot.r.p1 - pos >= state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols)
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_col = (f.dot.r.p1 - b) - state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols + 1;
    else if (pos - f.dot.r.p1 > 0)
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_col = f.dot.r.p1 - b;

    auto r = get_window_range(state.windows[state.file_id_to_window_id[state.file_state.active_file]], state.file_state);

    while (f.dot.r.p1 >= r.p2 && f.dot.r.p1 < f.content.size())
      {
      auto it = f.content.begin() + r.p2;
      auto it_end = f.content.end();
      bool last_line = false;
      for (; it != it_end; ++it, ++r.p2)
        {
        if (f.dot.r.p1 <= r.p2)
          last_line = true;
        if (*it == '\n')
          {
          auto le = get_line_length(f, state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos);
          state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos += le + 1;
          assert(has_valid_file_pos(state));
          if (last_line)
            break;
          }
        }
      if (it == it_end)
        {
        auto le = get_line_length(f, state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos);
        state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos += le + 1;
        assert(has_valid_file_pos(state));
        }
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row = 0;

      r = get_window_range(state.windows[state.file_id_to_window_id[state.file_state.active_file]], state.file_state);
      assert(r.p2 > f.dot.r.p1);
      }
    while (f.dot.r.p1 < r.p1)
      {
      auto it = f.content.rbegin() + (f.content.size() - r.p1);
      auto it_end = f.content.rend();
      //int64_t last_line_pos = r.p1;
      for (; it != it_end; ++it, --r.p1)
        {
        if (f.dot.r.p1 >= r.p1)
          break;
        if (*it == '\n')
          {
          //last_line_pos = r.p1;
          auto le = get_line_length(f, state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos - 1);
          state.windows[state.file_id_to_window_id[state.file_state.active_file]].file_pos -= le + 1;
          assert(has_valid_file_pos(state));
          }
        }
      //while (it != it_end && *it != '\n')
      //  {
      //  ++it;
      //  --r.p1;
      //  }
      //int64_t line_length = last_line_pos - r.p1;
      state.windows[state.file_id_to_window_id[state.file_state.active_file]].wordwrap_row = 0;// line_length / (state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1);
      r = get_window_range(state.windows[state.file_id_to_window_id[state.file_state.active_file]], state.file_state);
      assert(r.p1 <= f.dot.r.p1);
      }
    }

  // dealing with selection here (i.e. left or right shift is pressed)
  if (keyb_data.selecting != 0 && keyb_data.selection_id == state.file_state.active_file)
    {
    keyb_data.selection_end = f.dot.r.p1;
    }

  return state;
  }

app_state move_right(app_state state)
  {
  if (state.file_state.files[state.file_state.active_file].content.empty())
    return state;
  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;
  auto& f = state.file_state.files[state.file_state.active_file];
  ++f.dot.r.p2;
  if (f.dot.r.p2 > f.content.size())
    f.dot.r.p2 = f.content.size();
  f.dot.r.p1 = f.dot.r.p2;

  return check_boundaries(state, word_wrap);
  }

app_state move_left(app_state state)
  {
  if (state.file_state.files[state.file_state.active_file].content.empty())
    return state;
  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;
  auto& f = state.file_state.files[state.file_state.active_file];
  --f.dot.r.p1;
  if (f.dot.r.p1 < 0)
    f.dot.r.p1 = 0;
  f.dot.r.p2 = f.dot.r.p1;

  return check_boundaries(state, word_wrap);
  }

app_state move_to_begin_of_line(app_state state)
  {
  if (state.file_state.files[state.file_state.active_file].content.empty())
    return state;
  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;
  auto& f = state.file_state.files[state.file_state.active_file];
  int64_t p1 = get_begin_of_line(f);
  f.dot.r.p1 = p1;
  f.dot.r.p2 = p1;

  return check_boundaries(state, word_wrap);
  }

app_state move_to_end_of_line(app_state state)
  {
  if (state.file_state.files[state.file_state.active_file].content.empty())
    return state;
  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;
  auto& f = state.file_state.files[state.file_state.active_file];
  int64_t p1 = get_end_of_line(f);

  //if (p1 == f.content.size() - 1)
  //  ++p1;

  f.dot.r.p1 = p1;
  f.dot.r.p2 = p1;

  return check_boundaries(state, word_wrap);
  }

app_state text_input(app_state state, const char* txt);

int64_t get_piped_command_begin(const std::wstring& prompt, jamlib::file f)
  {
  int64_t p2 = f.content.size();
  f.dot.r.p1 = f.dot.r.p2 = p2;
  int64_t p1 = get_begin_of_line(f);
  std::wstring out(f.content.begin() + p1, f.content.begin() + p2);
  size_t find_prompt = out.find(prompt);
  if (find_prompt != std::wstring::npos)
    return p1 + find_prompt + prompt.size();
  return p1;
  }

app_state move_down(app_state state)
  {
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  auto& f = state.file_state.files[state.file_state.active_file];
  if (w.piped && on_last_line(f, f.dot.r.p1))
    {
    if (w.piped_prompt_history.empty())
      return state;
    auto text = w.piped_prompt_history[w.piped_prompt_index];
    if (w.piped_prompt_index == w.piped_prompt_history.size() - 1)
      w.piped_prompt_index = 0;
    else
      ++w.piped_prompt_index;

    int64_t p1 = get_piped_command_begin(w.piped_prompt, f);
    int64_t p2 = f.content.size();
    f.content = f.content.erase(p1, p2);
    f.dot.r.p1 = f.dot.r.p2 = p1;
    state = text_input(state, text.c_str());
    state.file_state.files[state.file_state.active_file].dot.r.p1 -= text.size();
    return state;
    }

  if (f.content.empty())
    return state;

  bool word_wrap = w.word_wrap;
  int64_t p1 = f.dot.r.p1;
  int64_t col = p1 - get_begin_of_line(f);
  int64_t ep = get_end_of_line(f);


  if (word_wrap)
    {
    int64_t line_length = ep - (p1 - col);
    if (line_length >= w.cols)
      {
      int64_t new_pos = p1 + w.cols - 1;
      if (new_pos > ep)
        {
        if (ep == f.content.size() - 1)
          {
          return check_boundaries(state, word_wrap);
          }
        int64_t empty_space = w.cols - (line_length % (w.cols - 1));
        new_pos -= empty_space - 2;
        if (new_pos < ep)
          new_pos = ep;
        }
      ++ep;
      if (ep > f.content.size())
        ep = f.content.size();
      f.dot.r.p1 = ep;
      f.dot.r.p2 = ep;
      ep = get_end_of_line(f);
      if (new_pos > ep)
        new_pos = ep;
      if (new_pos > f.content.size())
        new_pos = f.content.size();
      f.dot.r.p1 = new_pos;
      f.dot.r.p2 = new_pos;
      return check_boundaries(state, word_wrap);
      }
    }

  //if (ep == f.content.size() - 1)
  //  return check_boundaries(state, word_wrap);

  f.dot.r.p1 = ep + 1;
  f.dot.r.p2 = ep + 1;
  if (f.dot.r.p1 > f.content.size())
    {
    f.dot.r.p1 = f.dot.r.p2 = f.content.size();
    }
  p1 = get_begin_of_line(f);
  ep = get_end_of_line(f);
  if ((ep - p1) < col)
    {
    f.dot.r.p1 = ep;
    f.dot.r.p2 = ep;
    }
  else if (f.dot.r.p1 + col < f.content.size())
    {
    f.dot.r.p1 += col;
    f.dot.r.p2 += col;
    }

  return check_boundaries(state, word_wrap);
  }

app_state move_up(app_state state)
  {
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  auto& f = state.file_state.files[state.file_state.active_file];
  if (w.piped && on_last_line(f, f.dot.r.p1))
    {
    if (w.piped_prompt_history.empty())
      return state;
    auto text = w.piped_prompt_history[w.piped_prompt_index];
    if (w.piped_prompt_index == 0)
      w.piped_prompt_index = w.piped_prompt_history.size() - 1;
    else
      --w.piped_prompt_index;

    int64_t p1 = get_piped_command_begin(w.piped_prompt, f);
    int64_t p2 = f.content.size();
    f.content = f.content.erase(p1, p2);
    f.dot.r.p1 = f.dot.r.p2 = p1;
    state = text_input(state, text.c_str());
    state.file_state.files[state.file_state.active_file].dot.r.p1 -= text.size();
    return state;
    }

  if (f.content.empty())
    return state;
  bool word_wrap = w.word_wrap;

  int64_t p1 = f.dot.r.p1;
  int64_t bp = get_begin_of_line(f);

  if (word_wrap)
    {
    int64_t line_length = get_end_of_line(f) - bp;
    if (line_length >= w.cols)
      {
      int64_t new_pos = p1 - (w.cols - 1);
      if (new_pos < bp)
        {
        if (bp == 0)
          {
          return check_boundaries(state, word_wrap);
          }
        f.dot.r.p1 = bp - 1;
        f.dot.r.p2 = bp - 1;
        bp = get_begin_of_line(f);
        int64_t ep = get_end_of_line(f);
        line_length = ep - bp;
        int64_t empty_space = w.cols - (line_length % (w.cols - 1));
        new_pos += empty_space - 2;
        if (new_pos > ep)
          new_pos = ep;
        }
      if (new_pos < 0)
        new_pos = 0;
      f.dot.r.p1 = new_pos;
      f.dot.r.p2 = new_pos;
      return check_boundaries(state, word_wrap);
      }
    }

  if (bp == 0)
    return check_boundaries(state, word_wrap);

  int64_t col = p1 - bp;
  if (bp > 0)
    --bp;
  f.dot.r.p1 = bp;
  f.dot.r.p2 = bp;
  bp = get_begin_of_line(f);
  int64_t ep = get_end_of_line(f);

  if (word_wrap && ((ep - bp) >= w.cols))
    {
    while ((ep - bp) >= w.cols)
      bp += w.cols - 1;
    }

  if (ep - bp < col)
    {
    f.dot.r.p1 = ep;
    f.dot.r.p2 = ep;
    }
  else
    {
    f.dot.r.p1 = bp + col;
    f.dot.r.p2 = bp + col;
    }

  return check_boundaries(state, word_wrap);
  }

int64_t get_line_length(jamlib::file f, int64_t pos)
  {
  f.dot.r.p1 = pos;
  f.dot.r.p2 = pos;
  int64_t b = get_begin_of_line(f);
  int64_t e = get_end_of_line(f);
  return e - b;
  }

int64_t get_number_of_wrapped_lines(jamlib::file f, int64_t pos, int64_t nr_of_cols)
  {
  int64_t le = get_line_length(f, pos);
  return (le / (nr_of_cols - 1)) + 1;
  }

app_state move_page_up_without_cursor(app_state state, int64_t steps)
  {
  if (state.file_state.files[state.file_state.active_file].content.empty())
    return state;

  if (steps < 1)
    return state;

  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];

  auto& f = state.file_state.files[state.file_state.active_file];
  int64_t pos = w.file_pos;

  if (word_wrap)
    {

    w.wordwrap_row -= steps;

    while (w.wordwrap_row < 0)
      {
      if (w.file_pos <= 0)
        {
        w.wordwrap_row = 0;
        break;
        }
      int64_t le = pos ? get_line_length(f, pos - 1) : 0;
      w.file_pos -= le + 1;
      assert(has_valid_file_pos(state));
      w.wordwrap_row += (le / (w.cols - 1)) + 1;
      if (pos <= 0)
        break;
      pos = get_line_begin(f, pos - 1);
      if (pos < 0)
        pos = 0;
      }

    if (w.wordwrap_row < 0)
      w.wordwrap_row = 0;
    }
  else
    {
    auto it = f.content.rbegin() + (f.content.size() - w.file_pos);
    auto it_end = f.content.rend();

    int64_t file_pos_decreases = 0;

    int st = steps;

    while (it != it_end && st >= 0)
      {
      if (*it == '\n')
        {
        --st;
        }
      ++it;
      ++file_pos_decreases;
      }
    if (it == it_end)
      ++file_pos_decreases;
    w.file_pos -= file_pos_decreases - 1;
    if (w.file_pos < 0)
      w.file_pos = 0;
    assert(has_valid_file_pos(state));
    }

  return state;
  }


app_state move_page_down_without_cursor(app_state state, int64_t steps)
  {
  if (state.file_state.files[state.file_state.active_file].content.empty())
    return state;

  if (steps < 1)
    return state;

  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];

  std::stringstream str;
  str << "#" << w.file_pos << ", #" << w.file_pos << "+" << w.rows + w.wordwrap_row;
  auto fs = *jamlib::handle_command(state.file_state, str.str());
  auto f1 = fs.files[state.file_state.active_file];

  if (f1.dot.r.p2 == f1.content.size()) // at the end of the file
    {
    if (word_wrap)
      {
      int64_t p1 = f1.dot.r.p1;
      int64_t p2 = f1.dot.r.p2;

      auto it = f1.content.begin() + p1;
      auto it_end = f1.content.begin() + p2;

      int64_t row = 0;
      int64_t col = 0;

      int64_t pos = p1;
      for (; it != it_end; ++it, ++pos)
        {
        if (*it == '\n')
          {
          col = 0;
          ++row;
          }
        else
          {
          ++col;
          if (col >= w.cols - 1)
            {
            col = 0;
            ++row;
            }
          }
        if (row - w.wordwrap_row >= w.rows) // we didn't reach the end of the file yet
          break;
        }
      if (row - w.wordwrap_row < w.rows) // we reached the end of the file
        return state;
      }
    else
      {
      std::stringstream str2;
      str2 << "$-" << w.rows;
      auto fs2 = *jamlib::handle_command(state.file_state, str2.str());
      auto f2 = fs2.files[state.file_state.active_file];
      if (w.file_pos >= f2.dot.r.p1) // end of file
        return state;
      }
    }

  if (word_wrap)
    {
    int64_t p1 = f1.dot.r.p1;
    int64_t p2 = f1.dot.r.p2;

    auto it = f1.content.begin() + p1;
    auto it_end = f1.content.begin() + p2;

    int64_t last_row = 0;
    int64_t row = 0;
    int64_t col = 0;
    int64_t nr_of_wordwraps_passed = 0;

    w.wordwrap_row += steps;

    for (; it != it_end; ++it)
      {
      if (*it == '\n')
        {
        col = 0;
        ++row;
        auto wrapped_lines = row - last_row;
        if (wrapped_lines <= w.wordwrap_row)
          {
          auto le = get_line_length(f1, w.file_pos);
          w.file_pos += le + 1;
          assert(has_valid_file_pos(state));
          w.wordwrap_row -= wrapped_lines;
          }
        else
          break;
        last_row = row;
        }
      else
        {
        ++col;
        if (col >= w.cols - 1)
          {
          col = 0;
          ++row;
          ++nr_of_wordwraps_passed;
          }
        }
      }

    if (w.wordwrap_row > nr_of_wordwraps_passed)
      w.wordwrap_row = nr_of_wordwraps_passed;

    //state.windows[state.file_id_to_window_id[state.file_state.active_file]] = w;
    }
  else
    {
    auto it = f1.content.begin() + w.file_pos;
    auto it_end = f1.content.end();

    int64_t file_pos_increases = 0;

    int st = steps;

    while (it != it_end && st > 0)
      {
      if (*it == '\n')
        {
        --st;
        }
      ++it;
      ++file_pos_increases;
      }
    w.file_pos += file_pos_increases;
    //state.windows[state.file_id_to_window_id[state.file_state.active_file]] = w;
    }
  return state;

  }

app_state move_cursor_page_up(app_state state)
  {
  if (state.file_state.files[state.file_state.active_file].content.empty())
    return state;

  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  auto steps = state.windows[state.file_id_to_window_id[state.file_state.active_file]].rows - 1;
  auto& f = state.file_state.files[state.file_state.active_file];

  if (word_wrap)
    {

    w.wordwrap_row -= steps;
    /*
    while (w.wordwrap_row < 0)
      {
      --w.file_row;
      std::stringstream str;
      str << w.file_row + 1;
      auto fs = *jamlib::handle_command(state.file_state, str.str());
      w.wordwrap_row += get_number_of_wrapped_lines(fs.files[fs.active_file], fs.files[fs.active_file].dot.r.p1, w.cols);
      }
      */

    int64_t pos = w.file_pos;
    while (w.wordwrap_row < 0)
      {
      if (w.file_pos <= 0)
        {
        w.wordwrap_row = 0;
        break;
        }
      int64_t le = pos ? get_line_length(f, pos - 1) : 0;
      w.file_pos -= le + 1;
      assert(has_valid_file_pos(state));
      w.wordwrap_row += (le / (w.cols - 1)) + 1;
      if (pos <= 0)
        break;
      pos = get_line_begin(f, pos - 1);
      if (pos < 0)
        pos = 0;
      }

    if (w.wordwrap_row < 0)
      w.wordwrap_row = 0;
    }
  else
    {
    auto it = f.content.rbegin() + (f.content.size() - w.file_pos);
    auto it_end = f.content.rend();

    int64_t file_pos_decreases = 0;

    int st = steps;

    while (it != it_end && st >= 0)
      {
      if (*it == '\n')
        {
        --st;
        }
      ++it;
      ++file_pos_decreases;
      }
    if (it == it_end)
      ++file_pos_decreases;
    w.file_pos -= file_pos_decreases - 1;
    if (w.file_pos < 0)
      w.file_pos = 0;
    assert(has_valid_file_pos(state));
    }


  if (steps < 1)
    return check_boundaries(state, word_wrap);

  int64_t p1 = f.dot.r.p1;
  int64_t bp = get_begin_of_line(f);
  if (bp == 0 && !word_wrap)
    {
    return check_boundaries(state, word_wrap);
    }
  int64_t col = p1 - bp;
  if (word_wrap)
    {
    col = col % (state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1);
    bp = p1 - col;
    }

  int64_t pos = bp;
  auto it = f.content.begin() + pos;


  int64_t colcnt = state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1;

  while (pos > 0 && steps > 0)
    {
    --it;
    --pos;

    if (*it == '\n')
      {
      --steps;
      colcnt = 0;
      }
    else if (word_wrap)
      {
      ++colcnt;
      if (colcnt >= state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1)
        {
        colcnt = 0;
        --steps;
        }
      }
    }

  if (word_wrap)
    {
    // at this moment pos is in the correct wordwrap line, but its position can be arbitrary
    f.dot.r.p1 = pos;
    f.dot.r.p2 = pos;
    bp = get_begin_of_line(f);
    int64_t ep = get_end_of_line(f);
    int64_t tmp = (pos - bp) % (state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1);
    f.dot.r.p1 = pos - tmp + col;
    f.dot.r.p2 = pos - tmp + col;
    if (f.dot.r.p1 > ep)
      {
      f.dot.r.p1 = ep;
      f.dot.r.p2 = ep;
      }
    if (f.dot.r.p1 < bp)
      {
      f.dot.r.p1 = bp;
      f.dot.r.p2 = bp;
      }
    }
  else
    {
    f.dot.r.p1 = pos;
    f.dot.r.p2 = pos;
    bp = get_begin_of_line(f);
    int64_t ep = get_end_of_line(f);
    if (ep - bp < col)
      {
      f.dot.r.p1 = ep;
      f.dot.r.p2 = ep;
      }
    else
      {
      f.dot.r.p1 = bp + col;
      f.dot.r.p2 = bp + col;
      }
    }
  return check_boundaries(state, word_wrap);
  }

app_state move_cursor_page_down(app_state state)
  {

  if (state.file_state.files[state.file_state.active_file].content.empty())
    return state;
  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  auto steps = state.windows[state.file_id_to_window_id[state.file_state.active_file]].rows;
  auto& f = state.file_state.files[state.file_state.active_file];


  std::stringstream str;
  str << "#" << w.file_pos << ", #" << w.file_pos << "+" << w.rows + w.wordwrap_row;
  auto fs = *jamlib::handle_command(state.file_state, str.str());

  auto f1 = fs.files[state.file_state.active_file];

  if (f1.dot.r.p2 == f1.content.size()) // at the end of the file
    {
    if (word_wrap)
      {
      int64_t p1 = f1.dot.r.p1;
      int64_t p2 = f1.dot.r.p2;

      auto it = f1.content.begin() + p1;
      auto it_end = f1.content.begin() + p2;

      int64_t row = 0;
      int64_t col = 0;

      int64_t pos = p1;
      for (; it != it_end; ++it, ++pos)
        {
        if (*it == '\n')
          {
          col = 0;
          ++row;
          }
        else
          {
          ++col;
          if (col >= w.cols - 1)
            {
            col = 0;
            ++row;
            }
          }
        if (row - w.wordwrap_row >= w.rows) // we didn't reach the end of the file yet
          break;
        }
      if (row - w.wordwrap_row < w.rows) // we reached the end of the file
        {
        state.file_state.files[state.file_state.active_file].dot.r.p1 = state.file_state.files[state.file_state.active_file].dot.r.p2 = state.file_state.files[state.file_state.active_file].content.size();
        return state;
        }
      }
    else
      {
      std::stringstream str2;
      str2 << "$-" << w.rows;
      auto fs2 = *jamlib::handle_command(state.file_state, str2.str());
      auto f2 = fs2.files[state.file_state.active_file];
      if (w.file_pos >= f2.dot.r.p1) // end of file
        {
        state.file_state.files[state.file_state.active_file].dot.r.p1 = state.file_state.files[state.file_state.active_file].dot.r.p2 = state.file_state.files[state.file_state.active_file].content.size();
        return state;
        }
      }
    }


  if (word_wrap)
    {

    int64_t p1 = f1.dot.r.p1;
    int64_t p2 = f1.dot.r.p2;

    auto it = f1.content.begin() + p1;
    auto it_end = f1.content.begin() + p2;

    int64_t last_row = 0;
    int64_t row = 0;
    int64_t col = 0;

    w.wordwrap_row += steps - 1;

    for (; it != it_end; ++it)
      {
      if (*it == '\n')
        {
        col = 0;
        ++row;
        auto wrapped_lines = row - last_row;
        if (wrapped_lines <= w.wordwrap_row)
          {
          auto le = get_line_length(f, w.file_pos);
          w.file_pos += le + 1;
          assert(has_valid_file_pos(state));
          w.wordwrap_row -= wrapped_lines;
          }
        else
          break;
        last_row = row;
        }
      else
        {
        ++col;
        if (col >= w.cols - 1)
          {
          col = 0;
          ++row;
          }
        }
      }
    }
  else // not wordwrap
    {
    auto it = f.content.begin() + w.file_pos;
    auto it_end = f.content.end();

    int64_t file_pos_increases = 0;

    int st = steps - 1;

    while (it != it_end && st > 0)
      {
      if (*it == '\n')
        {
        --st;
        }
      ++it;
      ++file_pos_increases;
      }
    w.file_pos += file_pos_increases;
    }

  if (steps < 1)
    return check_boundaries(state, word_wrap);

  int64_t p1 = f.dot.r.p1;
  int64_t col = p1 - get_begin_of_line(f);

  if (word_wrap)
    col = col % (state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1);

  int64_t pos = p1 - col;

  auto it = f.content.begin() + pos; // this is beginning of line (both wordwrap or not)
  auto it_end = f.content.end();

  int64_t wordwrap_row_increases = 0;
  int64_t colcnt = 0;

  while (it != it_end && steps > 0)
    {
    if (*it == '\n')
      {
      --steps;
      colcnt = 0;
      }
    else if (word_wrap)
      {
      ++colcnt;
      if (colcnt >= state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1)
        {
        colcnt = 0;
        ++wordwrap_row_increases;
        --steps;
        }
      }
    ++it;
    ++pos;
    }
  state.windows[state.file_id_to_window_id[state.file_state.active_file]] = w;
  assert(has_valid_file_pos(state));

  f.dot.r.p1 = pos == f.content.size() ? pos : pos - 1;
  f.dot.r.p2 = pos == f.content.size() ? pos : pos - 1;
  if (f.dot.r.p1 > f.content.size())
    {
    f.dot.r.p1 = f.dot.r.p2 = f.content.size();
    }

  p1 = get_begin_of_line(f);

  if (word_wrap)
    {
    int64_t wordwraplength = (pos - 1 - p1) % (state.windows[state.file_id_to_window_id[state.file_state.active_file]].cols - 1);
    int64_t newpos = pos - 1 - wordwraplength;
    newpos += col;
    if (newpos > pos - 1 && (pos != f.content.size() || newpos != f.content.size()))
      newpos = pos - 1;
    f.dot.r.p1 = newpos;
    f.dot.r.p2 = newpos;
    }
  else
    {
    int64_t ep = get_end_of_line(f);
    if ((ep - p1) < col)
      {
      f.dot.r.p1 = ep;
      f.dot.r.p2 = ep;
      }
    else if (f.dot.r.p1 + col < f.content.size())
      {
      f.dot.r.p1 = p1 + col;
      f.dot.r.p2 = p1 + col;
      }
    }

  return check_boundaries(state, word_wrap);
  }

void invalidate_column_item(app_state state, uint64_t c, uint64_t ci)
  {
  int icols = get_cols();

  auto& column = state.g.columns[c];

  auto& col_item = column.items[ci];

  int left = (int)std::round(column.left*icols);
  int right = (int)std::round(column.right*icols);

  int irows = get_available_rows(column, right - left, state);

  int top = (int)(col_item.top_layer*irows);
  int bottom = (int)(col_item.bottom_layer*irows);

  auto y_offset = get_y_offset_from_top(column, right - left, state);

  invalidate_range(left - OFFSET_FROM_SCROLLBAR, y_offset, right - left + OFFSET_FROM_SCROLLBAR, bottom - top);
  }

app_state move_column(app_state state, uint64_t c, int x, int y)
  {
  int icols = get_cols();
  int irows = get_lines();

  auto& column = state.g.columns[c];

  int left = (int)std::round(column.left*icols);
  int right = (int)std::round(column.right*icols);

  if (left < x)
    {
    if (!c)
      return state;
    while (x >= right)
      --x;
    if (x < right)
      {
      while (right - x < 2) // leave at least column width 2 for collapsing column
        --x;
      column.left = x / double(icols);
      state.g.columns[c - 1].right = column.left;
      return resize(state);
      }
    }
  else // left > x
    {
    if (!c)
      return state;
    int leftleft = (int)(state.g.columns[c - 1].left*icols);
    while (x <= leftleft)
      ++x;
    if (leftleft < x)
      {
      while (x - leftleft < 2) // leave at least column width 2 for collapsing column
        ++x;
      column.left = x / double(icols);
      state.g.columns[c - 1].right = column.left;
      return resize(state);
      }
    }
  return state;
  }

app_state move_window_to_other_column(app_state state, uint64_t c, uint64_t ci, int x, int y)
  {
  int icols = get_cols();

  uint64_t target_c = 0;
  for (; target_c < state.g.columns.size(); ++target_c)
    {
    auto& local_col = state.g.columns[target_c];

    int left = (int)std::round(local_col.left*icols);
    int right = (int)std::round(local_col.right*icols);

    if (x >= left && x <= right)
      break;
    }

  if (c == target_c)
    return state;

  auto& source_column = state.g.columns[c];
  auto& target_column = state.g.columns[target_c];

  auto item = source_column.items[ci];

  if (ci)
    source_column.items[ci - 1].bottom_layer = item.bottom_layer;
  else if (ci + 1 < source_column.items.size())
    source_column.items[ci + 1].top_layer = item.top_layer;

  source_column.items.erase(source_column.items.begin() + ci);

  int left = (int)(target_column.left*icols);
  int right = (int)(target_column.right*icols);

  int irows = get_available_rows(target_column, right - left, state);

  y -= get_y_offset_from_top(target_column, right - left, state);
  if (y < 0)
    y = 0;

  uint64_t new_pos = 0;
  for (; new_pos < target_column.items.size(); ++new_pos)
    {
    int top = (int)std::round(target_column.items[new_pos].top_layer * irows);
    if (top > y)
      break;
    }

  target_column.items.insert(target_column.items.begin() + new_pos, item);
  if (new_pos > 0)
    {
    double new_bot = (y) / double(irows);
    target_column.items[new_pos].bottom_layer = target_column.items[new_pos - 1].bottom_layer;
    target_column.items[new_pos - 1].bottom_layer = new_bot;
    target_column.items[new_pos].top_layer = new_bot;
    }
  else if (new_pos + 1 < target_column.items.size())
    {
    if (target_column.items[new_pos + 1].top_layer != 0.0)
      {
      target_column.items[new_pos].bottom_layer = target_column.items[new_pos + 1].top_layer;
      target_column.items[new_pos].top_layer = 0.0;
      }
    else
      {
      double new_top = (target_column.items[new_pos + 1].top_layer + target_column.items[new_pos + 1].bottom_layer)*0.5;
      target_column.items[new_pos + 1].top_layer = new_top;
      target_column.items[new_pos].bottom_layer = new_top;
      target_column.items[new_pos].top_layer = 0.0;
      }
    }
  else
    {
    target_column.items[new_pos].top_layer = 0.0;
    target_column.items[new_pos].bottom_layer = 1.0;
    }
  return resize(state);
  }

app_state move_window_to_top(app_state state, uint64_t c, int64_t ci)
  {
  auto& column = state.g.columns[c];
  auto col_item = column.items[ci];
  for (int i = ci - 1; i >= 0; --i)
    column.items[i + 1] = column.items[i];
  column.items[0] = col_item;  
  return *optimize_column(state, state.windows[state.window_pairs[col_item.window_pair_id].window_id].file_id);
  }

app_state move_window_up_down(app_state state, uint64_t c, int64_t ci, int x, int y)
  {
  int icols = get_cols();

  auto& column = state.g.columns[c];

  auto& col_item = column.items[ci];

  int left = (int)std::round(column.left*icols);
  int right = (int)std::round(column.right*icols);

  int irows = get_available_rows(column, right - left, state);

  int top = (int)std::round(col_item.top_layer*irows);
  int bottom = (int)std::round(col_item.bottom_layer*irows);

  auto y_offset = get_y_offset_from_top(column, right - left, state);

  y -= y_offset;

  if (y < top)
    {
    // First we check whether we want to move this item to the top.
    if (ci) // if ci == 0, then it is already at the top
      {
      int top_top = (int)std::round(column.items[0].top_layer*irows);
      if (y < top_top)
        return move_window_to_top(state, c, ci);
      }

    int minimum_size_for_higher_items = 0;
    for (int64_t other = (int64_t)ci - 1; other >= 0; --other)
      minimum_size_for_higher_items += get_minimum_number_of_rows(column.items[other], right - left, state);
    if (y < minimum_size_for_higher_items)
      y = minimum_size_for_higher_items;

    col_item.top_layer = (y) / double(irows);
    double last_top_layer = col_item.top_layer;
    for (int other_ci = ci - 1; other_ci >= 0; --other_ci)
      {
      auto& other_col_item = column.items[other_ci];
      other_col_item.bottom_layer = last_top_layer;
      if (other_col_item.top_layer >= other_col_item.bottom_layer)
        {
        other_col_item.top_layer = ((int)(other_col_item.bottom_layer*irows) - 1) / double(irows);
        }
      last_top_layer = other_col_item.top_layer;
      }
    }
  else if (y >= top && y < bottom)
    {
    int minimum_nr_rows = get_minimum_number_of_rows(col_item, right - left, state);

    // y + minimum_nr_rows > bottom
    while (bottom < (y + minimum_nr_rows))
      --y;
    col_item.top_layer = (y) / double(irows);
    if (ci > 0)
      column.items[ci - 1].bottom_layer = col_item.top_layer;
    else
      {
      invalidate_range(left, y_offset, right - left, bottom - top);
      }
    }
  else
    {
    int minimum_nr_rows = get_minimum_number_of_rows(col_item, right - left, state);
    col_item.top_layer = ((int)std::round(col_item.bottom_layer*irows) - minimum_nr_rows) / double(irows);
    if (ci > 0)
      column.items[ci - 1].bottom_layer = col_item.top_layer;
    else
      {
      invalidate_range(left, y_offset, right - left, bottom - top);
      }
    }
  return resize(state);
  }

app_state enlarge_window_as_much_as_possible(app_state state, int64_t file_id)
  {
  int64_t win_id = state.file_id_to_window_id[file_id];
  for (uint64_t c = 0; c < state.g.columns.size(); ++c)
    {
    auto& column = state.g.columns[c];
    for (uint64_t ci = 0; ci < column.items.size(); ++ci)
      {
      uint32_t win_pair = column.items[ci].window_pair_id;
      if (state.window_pairs[win_pair].window_id == win_id || state.window_pairs[win_pair].command_window_id == win_id)
        {
        column.maximized = false;
        int icols = get_cols();
        auto& col_item = column.items[ci];

        int left = (int)std::round(column.left*icols);
        int right = (int)std::round(column.right*icols);

        int irows = get_available_rows(column, right - left, state);

        int minimum_size_for_higher_items = 0;
        for (int64_t other = (int64_t)ci - 1; other >= 0; --other)
          minimum_size_for_higher_items += get_minimum_number_of_rows(column.items[other], right - left, state);

        int top = minimum_size_for_higher_items;

        int minimum_size_for_lower_items = 0;
        for (uint64_t other = ci + 1; other < column.items.size(); ++other)
          minimum_size_for_lower_items += get_minimum_number_of_rows(column.items[other], right - left, state);


        int bottom = irows - minimum_size_for_lower_items;

        double new_top = top / (double)irows;
        double new_bottom = bottom / (double)irows;

        col_item.top_layer = new_top;
        col_item.bottom_layer = new_bottom;

        for (uint64_t other = ci + 1; other < column.items.size(); ++other)
          {
          column.items[other].top_layer = new_bottom;
          bottom += get_minimum_number_of_rows(column.items[other], right - left, state);
          new_bottom = bottom / (double)irows;
          column.items[other].bottom_layer = new_bottom;
          }
        for (int64_t other = (int64_t)ci - 1; other >= 0; --other)
          {
          column.items[other].bottom_layer = new_top;
          top -= get_minimum_number_of_rows(column.items[other], right - left, state);
          new_top = top / (double)irows;
          column.items[other].top_layer = new_top;
          }

        return resize(state);
        }
      }
    }
  return state;
  }


app_state enlarge_window(app_state state, int64_t file_id)
  {
  int64_t win_id = state.file_id_to_window_id[file_id];
  for (uint64_t c = 0; c < state.g.columns.size(); ++c)
    {
    auto& column = state.g.columns[c];
    for (uint64_t ci = 0; ci < column.items.size(); ++ci)
      {
      uint32_t win_pair = column.items[ci].window_pair_id;
      if (state.window_pairs[win_pair].window_id == win_id || state.window_pairs[win_pair].command_window_id == win_id)
        {
        if (column.maximized)
          {
          column.maximized = false;
          return enlarge_window_as_much_as_possible(state, file_id);
          }
        int icols = get_cols();
        auto& col_item = column.items[ci];

        int left = (int)std::round(column.left*icols);
        int right = (int)std::round(column.right*icols);

        int irows = get_available_rows(column, right - left, state);

        int bottom = (int)std::round(col_item.bottom_layer*irows);

        int minimum_size_for_lower_items = 0;
        for (uint64_t other = ci + 1; other < column.items.size(); ++other)
          minimum_size_for_lower_items += get_minimum_number_of_rows(column.items[other], right - left, state);

        ++bottom;
        while (bottom > irows - minimum_size_for_lower_items)
          --bottom;


        if (bottom == (int)std::round(col_item.bottom_layer*irows)) // could not enlarge at the bottom, try the top
          {
          int top = (int)std::round(col_item.top_layer*irows);

          int minimum_size_for_upper_items = 0;
          for (uint64_t other = 0; other < ci; ++other)
            minimum_size_for_upper_items += get_minimum_number_of_rows(column.items[other], right - left, state);

          --top;
          while (top < minimum_size_for_upper_items)
            ++top;

          double new_top = top / (double)irows;
          col_item.top_layer = new_top;
          for (int64_t other = ci - 1; other >= 0; --other)
            {
            column.items[other].bottom_layer = new_top;
            int bottom = top;
            top = (int)std::round(column.items[other].top_layer*irows);
            new_top = column.items[other].top_layer;
            if (bottom - top < get_minimum_number_of_rows(column.items[other], right - left, state))
              {
              top = bottom - get_minimum_number_of_rows(column.items[other], right - left, state);
              new_top = top / (double)irows;
              }
            column.items[other].top_layer = new_top;
            }
          }
        else
          {
          double new_bottom = bottom / (double)irows;

          col_item.bottom_layer = new_bottom;

          for (uint64_t other = ci + 1; other < column.items.size(); ++other)
            {
            column.items[other].top_layer = new_bottom;
            int top = bottom;
            bottom = (int)std::round(column.items[other].bottom_layer*irows);
            new_bottom = column.items[other].bottom_layer;
            if (bottom - top < get_minimum_number_of_rows(column.items[other], right - left, state))
              {
              bottom = top + get_minimum_number_of_rows(column.items[other], right - left, state);
              new_bottom = bottom / (double)irows;
              }
            column.items[other].bottom_layer = new_bottom;
            }
          }

        int top = (int)std::round(col_item.top_layer*irows) + get_y_offset_from_top(column, right - left, state);
        SDL_WarpMouseInWindow(pdc_window, left*font_width + font_width / 2.0, top*font_height + font_height / 2.0); // move mouse on icon, so that you can keep clicking

        return resize(state);
        }
      }
    }
  return state;
  }

app_state maximize_window(app_state state, int64_t file_id)
  {
  int64_t win_id = state.file_id_to_window_id[file_id];
  for (uint64_t c = 0; c < state.g.columns.size(); ++c)
    {
    auto& column = state.g.columns[c];
    for (uint64_t ci = 0; ci < column.items.size(); ++ci)
      {
      uint32_t win_pair = column.items[ci].window_pair_id;
      if (state.window_pairs[win_pair].window_id == win_id || state.window_pairs[win_pair].command_window_id == win_id)
        {
        column.maximized = true;
        int icols = get_cols();
        auto& col_item = column.items[ci];

        col_item.top_layer = 0.0;
        col_item.bottom_layer = 1.0;

        for (uint64_t other = ci + 1; other < column.items.size(); ++other)
          {
          column.items[other].top_layer = 1.0;
          column.items[other].bottom_layer = 1.0;
          }
        for (int64_t other = (int64_t)ci - 1; other >= 0; --other)
          {
          column.items[other].top_layer = 0.0;
          column.items[other].bottom_layer = 0.0;
          }

        return resize(state);
        }
      }
    }
  return state;
  }

app_state adapt_grid(app_state state, int x, int y)
  {
  int icols = get_cols();
  int irows = get_lines();

  int64_t win_id = state.file_id_to_window_id[mouse.rwd.rearranging_file_id];
  for (uint64_t c = 0; c < state.g.columns.size(); ++c)
    {
    if (state.g.columns[c].column_command_window_id == win_id)
      return move_column(state, c, x, y);

    auto& column = state.g.columns[c];

    int left = (int)std::round(column.left*icols);
    int right = (int)std::round(column.right*icols);

    for (uint64_t ci = 0; ci < column.items.size(); ++ci)
      {
      uint32_t win_pair = column.items[ci].window_pair_id;
      if (state.window_pairs[win_pair].window_id == win_id || state.window_pairs[win_pair].command_window_id == win_id)
        {
        if (x >= left - 2 && x <= right + 2)
          return move_window_up_down(state, c, ci, x, y);
        else
          return move_window_to_other_column(state, c, ci, x, y);
        }
      }
    }
  return state;
  }

int64_t get_active_file_id(const app_state& state, int64_t id)
  {
  auto& w = state.windows[state.file_id_to_window_id[id]];
  if (w.is_command_window)
    {
    if (w.nephew_id != (uint32_t)-1)
      return (int64_t)w.nephew_id;
    }
  return id;
  }

int compute_rows_necessary(const app_state& state, int number_of_column_cols, int number_of_rows_available, uint32_t window_pair_id)
  {
  uint32_t column_id = state.window_pairs[window_pair_id].command_window_id;
  uint32_t window_id = state.window_pairs[window_pair_id].window_id;
  uint32_t command_length = state.file_state.files[state.windows[column_id].file_id].content.size();
  uint32_t command_rows = number_of_column_cols > ICON_LENGTH + 1 ? command_length / (number_of_column_cols - 1 - ICON_LENGTH) + 1 : 0;
  const auto& w = state.windows[window_id];
  const auto& f = state.file_state.files[w.file_id].content;
  int row = 0;
  int col = 0;
  auto it = f.begin();
  auto it_end = f.end();
  for (; it != it_end; ++it)
    {
    if (*it == '\n')
      {
      col = 0;
      ++row;
      }
    else if (w.word_wrap)
      {
      ++col;
      if (col >= w.cols - 1)
        {
        col = 0;
        ++row;
        }
      }
    else
      ++col;
    if (row >= number_of_rows_available - command_rows)
      break;
    }
  if (col != 0)
    ++row;
  ++row;
  if (row < 4)
    row = 4;
  return row + command_rows;
  }

/*
The window appears in the ‘active’ column, that most recently used
for typing or selecting. Executing and searching do not affect the choice of active column, so windows of commands
and such do not draw new windows towards them, but rather let them form near the targets of their actions.
Output (error) windows always appear towards the right, away from edited text, which is typically kept towards the
left. Within the column, several competing desires are balanced to decide where and how large the window should
be: large blank spaces should be consumed; existing text should remain visible; existing large windows should be
divided before small ones; and the window should appear near the one containing the action that caused its creation
*/

std::optional<app_state> optimize_column(app_state state, int64_t id)
  {
  for (uint32_t i = 0; i < state.g.columns.size(); ++i)
    {
    auto& c = state.g.columns[i];
    for (const auto& ci : c.items)
      {
      auto& wp = state.window_pairs[ci.window_pair_id];
      if (state.windows[wp.window_id].file_id == id)
        {
        int icols = get_cols();
        int left = (int)std::round(c.left*icols);
        int right = (int)std::round(c.right*icols);
        int irows = get_available_rows(c, right - left, state);

        std::vector<int> nr_of_rows_necessary;
        int total_rows = 0;
        for (int j = 0; j < c.items.size(); ++j)
          {
          int top = (int)std::round(c.items[j].top_layer*irows);
          int bottom = (int)std::round(c.items[j].bottom_layer*irows);
          int current_rows = bottom - top;
          nr_of_rows_necessary.push_back(compute_rows_necessary(state, right - left, irows, c.items[j].window_pair_id));
          if (nr_of_rows_necessary.back() > current_rows)
            nr_of_rows_necessary.back() = current_rows;
          total_rows += nr_of_rows_necessary.back();
          }
        if (total_rows > irows)
          {
          for (auto& r : nr_of_rows_necessary)
            {
            double d = (double)r / (double)total_rows * (double)irows;
            r = (int)std::floor(d);
            if (r <= 0)
              ++r;
            }
          }
        else // all windows can be visualized
          {
          int extra_per_item = (irows - total_rows) / c.items.size();
          int remainder = (irows - total_rows) % c.items.size();
          for (int j = 0; j < nr_of_rows_necessary.size(); ++j)
            {
            nr_of_rows_necessary[j] += extra_per_item;
            if (j < remainder)
              nr_of_rows_necessary[nr_of_rows_necessary.size() - j - 1] += 1;
            }
          for (int j = 0; j < c.items.size(); ++j)
            {
            auto wp = c.items[j].window_pair_id;
            auto& w = state.windows[state.window_pairs[wp].window_id];
            w.file_pos = 0;
            w.wordwrap_row = 0;
            }
          }
        c.items.front().top_layer = 0.0;
        for (int j = 0; j < c.items.size(); ++j)
          {
          c.items[j].bottom_layer = ((double)nr_of_rows_necessary[j] + c.items[j].top_layer*irows) / (double)irows;
          if (j + 1 < c.items.size())
            c.items[j + 1].top_layer = c.items[j].bottom_layer;
          }
        c.items.back().bottom_layer = 1.0;
        return resize(state);
        }
      }
    }
  return resize(state);
  }

std::optional<app_state> new_column_command(app_state state, int64_t id, const std::string&)
  {
  int icols = get_cols();

  column c;
  state.file_state.files.emplace_back();
  auto command_window = state.file_state.files.back().content.transient();
  std::string text = gp_settings->column_text;
  for (auto ch : text)
    command_window.push_back(ch);
  state.file_state.files.back().content = command_window.persistent();
  state.file_state.files.back().dot.r.p1 = command_window.size();
  state.file_state.files.back().dot.r.p2 = command_window.size();
  state.file_id_to_window_id.push_back((uint32_t)state.windows.size());
  state.windows.emplace_back(0, 0, icols, 1, (uint32_t)state.file_state.files.size() - 1, (uint32_t)-1, true);
  c.column_command_window_id = (uint32_t)state.windows.size() - 1;

  c.left = 0.0;
  if (!state.g.columns.empty())
    {
    if (state.g.columns.back().right == 1.0)
      state.g.columns.back().right = (state.g.columns.back().right - state.g.columns.back().left) / 2.0 + state.g.columns.back().left;
    c.left = state.g.columns.back().right;
    }
  c.right = 1.0;
  state.g.columns.push_back(c);
  return resize(state);
  }

app_state add_error_window(app_state state)
  {
  if (state.g.columns.empty())
    state = *new_column_command(state, 0, "");

  assert(!state.g.columns.empty());

  int icols = get_cols();
  int irows = get_lines();

  std::string error_filename("+Errors");

  state.file_state.files.emplace_back();
  auto command_window = state.file_state.files.back().content.transient();
  state.file_state.files.back().filename = error_filename;
  auto text = make_command_text(state, state.file_state.files.size() - 1);
  for (auto ch : text)
    command_window.push_back(ch);


  state.file_state.files.back().content = command_window.persistent();
  state.file_state.files.back().dot.r.p1 = command_window.size();
  state.file_state.files.back().dot.r.p2 = command_window.size();
  state.file_state.files.back().enc = jamlib::ENC_UTF8;
  state.file_id_to_window_id.push_back((uint32_t)state.windows.size());
  state.file_state.files.emplace_back();
  state.file_state.files.back().dot.r.p1 = 0;
  state.file_state.files.back().dot.r.p2 = 0;
  state.file_state.files.back().enc = jamlib::ENC_UTF8;
  state.file_state.files.back().filename = error_filename;
  state.file_id_to_window_id.push_back((uint32_t)state.windows.size() + 1);


  state.windows.emplace_back(0, 0, icols, 1, (uint32_t)state.file_state.files.size() - 2, (uint32_t)state.file_state.files.size() - 1, true);
  state.windows.emplace_back(0, 1, icols, irows - 2, (uint32_t)state.file_state.files.size() - 1, (uint32_t)state.file_state.files.size() - 2, false);

  state.window_pairs.emplace_back(0, 0, icols, irows, (uint32_t)state.windows.size() - 1, (uint32_t)state.windows.size() - 2);

  column_item ci;
  ci.column_id = state.g.columns.size() - 1;
  ci.top_layer = 0.0;
  ci.bottom_layer = 1.0;
  ci.window_pair_id = (uint32_t)(state.window_pairs.size() - 1);
  if (!state.g.columns.back().items.empty())
    {
    state.g.columns.back().items.back().bottom_layer = (state.g.columns.back().items.back().bottom_layer + state.g.columns.back().items.back().top_layer)*0.5;
    ci.top_layer = state.g.columns.back().items.back().bottom_layer;
    }
  state.g.columns.back().items.push_back(ci);
  return *optimize_column(state, state.file_state.files.size() - 1);
  }

app_state add_error_text(app_state state, const std::string& errortext)
  {
  std::string error_filename("+Errors");
  int64_t file_id = -1;
  for (const auto& w : state.windows)
    {
    if (!w.is_command_window)
      {
      const auto& f = state.file_state.files[w.file_id];
      if (f.filename == error_filename)
        file_id = w.file_id;
      }
    }

  if (file_id < 0)
    {
    state = add_error_window(state);
    file_id = state.file_state.files.size() - 1;
    }
  auto active = state.file_state.active_file;

  state.file_state.active_file = file_id;
  state.file_state.files[file_id].dot.r.p1 = state.file_state.files[file_id].content.size();
  state.file_state.files[file_id].dot.r.p2 = state.file_state.files[file_id].content.size();
  bool added_newline = false;
  std::stringstream ss;
  ss << "a/";
  if (!state.file_state.files[file_id].content.empty())
    {
    ss << resolve_jamlib_escape_characters("\n");
    added_newline = true;
    }
  ss << resolve_jamlib_escape_characters(errortext) << "/";
  state.file_state = *jamlib::handle_command(state.file_state, ss.str());
  if (added_newline)
    ++state.file_state.files[file_id].dot.r.p1;
  state = check_boundaries(state, state.windows[state.file_id_to_window_id[file_id]].word_wrap);
  state.file_state.active_file = active;
  return state;

  /*
  auto text_window = state.file_state.files[file_id].content.transient();
  if (!text_window.empty())
    text_window.push_back('\n');
  auto werrortext = JAM::convert_string_to_wstring(errortext);
  for (auto ch : werrortext)
    text_window.push_back(ch);
  state.file_state.files[file_id].content = text_window.persistent();

  state.file_state.files[file_id].dot.r.p1 = text_window.size();
  state.file_state.files[file_id].dot.r.p2 = text_window.size();
  state.file_state.files[file_id].enc = jamlib::ENC_UTF8;


  state.file_state.active_file = file_id;
  state = check_boundaries(state, state.windows[state.file_id_to_window_id[file_id]].word_wrap);
  state.file_state.active_file = active;
  return state;
  //return *optimize_column(state, file_id);
  */
  }

std::optional<app_state> exit_command(app_state state, int64_t, const std::string&)
  {
  std::stringstream str;
  bool show_error_window = false;
  for (uint32_t i = 0; i < state.g.columns.size(); ++i)
    {
    auto& c = state.g.columns[i];
    for (const auto& ci : c.items)
      {
      auto& wp = state.window_pairs[ci.window_pair_id];
      auto& f = state.file_state.files[state.windows[wp.window_id].file_id];
      if (ask_user_to_save_modified_file(f))
        {
        show_error_window = true;
        str << f.filename << " modified\n";
        f.modification_mask = 2;
        }
      }
    }
  if (show_error_window)
    {
    return add_error_text(state, str.str());
    }
  return std::nullopt;
  }

std::optional<app_state> del_window(app_state state, int64_t id, const std::string&)
  {
  for (uint32_t i = 0; i < state.g.columns.size(); ++i)
    {
    auto& c = state.g.columns[i];
    for (uint32_t j = 0; j < c.items.size(); ++j)
      {
      auto ci = c.items[j];
      auto& wp = state.window_pairs[ci.window_pair_id];
      if (state.windows[wp.command_window_id].file_id == id || state.windows[wp.window_id].file_id == id)
        {
        auto& w = state.windows[wp.window_id];
        auto& f = state.file_state.files[w.file_id];
        if (ask_user_to_save_modified_file(f))
          {
          f.modification_mask = 2;
          std::stringstream str;
          str << f.filename << " modified";
          return add_error_text(state, str.str());
          }
        else
          {
          invalidate_column_item(state, i, j);
          c.items.erase(c.items.begin() + j);

          int64_t f1 = w.file_id;
          int64_t f2 = w.nephew_id;
          if (f1 > f2)
            std::swap(f1, f2);
          state.file_state.files.erase(state.file_state.files.begin() + f2);
          state.file_state.files.erase(state.file_state.files.begin() + f1);

          int64_t w1 = state.file_id_to_window_id[f1];
          int64_t w2 = state.file_id_to_window_id[f2];
          if (w1 > w2)
            std::swap(w1, w2);

          state.file_id_to_window_id.erase(state.file_id_to_window_id.begin() + f2);
          state.file_id_to_window_id.erase(state.file_id_to_window_id.begin() + f1);

          auto wp_id = ci.window_pair_id;
          state.window_pairs.erase(state.window_pairs.begin() + ci.window_pair_id);

          state.windows[w1].kill_pipe();
          state.windows[w2].kill_pipe();

          state.windows.erase(state.windows.begin() + w2);
          state.windows.erase(state.windows.begin() + w1);

          for (auto& win : state.windows)
            {
            if (win.file_id > f2)
              --win.file_id;
            if (win.file_id > f1)
              --win.file_id;
            if (win.nephew_id > f2 && win.nephew_id != (uint32_t)-1)
              --win.nephew_id;
            if (win.nephew_id > f1 && win.nephew_id != (uint32_t)-1)
              --win.nephew_id;
            }
          for (auto& fid2winid : state.file_id_to_window_id)
            {
            if (fid2winid > w2)
              --fid2winid;
            if (fid2winid > w1)
              --fid2winid;
            }
          for (auto& winp : state.window_pairs)
            {
            if (winp.command_window_id > w2)
              --winp.command_window_id;
            if (winp.command_window_id > w1)
              --winp.command_window_id;
            if (winp.window_id > w2)
              --winp.window_id;
            if (winp.window_id > w1)
              --winp.window_id;
            }
          if (state.g.topline_window_id > w2)
            --state.g.topline_window_id;
          if (state.g.topline_window_id > w1)
            --state.g.topline_window_id;
          for (auto& col : state.g.columns)
            {
            if (col.column_command_window_id > w2)
              --col.column_command_window_id;
            if (col.column_command_window_id > w1)
              --col.column_command_window_id;
            for (auto& colitem : col.items)
              {
              if (colitem.window_pair_id > wp_id)
                --colitem.window_pair_id;
              }
            }
          if (state.file_state.active_file == f1 || state.file_state.active_file == f2)
            {
            if (c.items.empty())
              state.file_state.active_file = 0;
            else
              {
              if (j >= c.items.size())
                j = c.items.size() - 1;
              state.file_state.active_file = state.windows[state.window_pairs[c.items[j].window_pair_id].window_id].file_id;
              }
            }
          else
            {
            if (state.file_state.active_file > f2)
              --state.file_state.active_file;
            if (state.file_state.active_file > f1)
              --state.file_state.active_file;
            }
          if (state.g.columns[i].items.empty())
            return state;
          return optimize_column(state, state.windows[state.window_pairs[state.g.columns[i].items.back().window_pair_id].window_id].file_id);
          }
        }
      }
    }
  return state;
  }

std::optional<app_state> del_column(app_state state, int64_t id, const std::string&)
  {
  for (uint32_t i = 0; i < state.g.columns.size(); ++i)
    {
    auto& c = state.g.columns[i];
    if (state.windows[c.column_command_window_id].file_id == id)
      {
      std::stringstream str;
      bool show_error_window = false;
      for (const auto& ci : c.items)
        {
        auto& wp = state.window_pairs[ci.window_pair_id];
        auto& f = state.file_state.files[state.windows[wp.window_id].file_id];
        if (ask_user_to_save_modified_file(f))
          {
          show_error_window = true;
          str << f.filename << " modified\n";
          f.modification_mask = 2;
          }
        }
      if (show_error_window)
        {
        return add_error_text(state, str.str());
        }
      else
        {
        int sz = c.items.size();
        for (int j = sz - 1; j >= 0; --j)
          {
          auto ci = state.g.columns[i].items[j];
          int64_t id = state.windows[state.window_pairs[ci.window_pair_id].window_id].file_id;
          state = *del_window(state, id, "");
          }
        double right = state.g.columns[i].right;
        if (i)
          state.g.columns[i - 1].right = right;
        else if (state.g.columns.size() > 1)
          state.g.columns[1].left = 0.0;
        state.g.columns.erase(state.g.columns.begin() + i);
        return resize(state);
        }
      }
    }
  return state;
  }

uint32_t get_column_id(const app_state& state, int64_t id);

std::optional<app_state> new_window(app_state state, int64_t id, const std::string&)
  {
  //id = state.file_state.active_file;

  if (state.g.columns.empty())
    state = *new_column_command(state, id, "");

  uint32_t column_id = get_column_id(state, id);

  if (column_id == get_column_id(state, state.file_state.active_file)) // if the active file is in the column where we clicked on "New", then make new window below active window
    {
    id = state.file_state.active_file;
    }

  assert(column_id != (uint32_t)-1);
  assert(!state.g.columns.empty());

  int icols = get_cols();
  int irows = get_lines();

  state.file_state.files.emplace_back();
  auto command_window = state.file_state.files.back().content.transient();
  //std::string text = " " + gp_settings->command_text;
  auto text = make_command_text(state, state.file_state.files.size() - 1);
  for (auto ch : text)
    command_window.push_back(ch);
  state.file_state.files.back().content = command_window.persistent();
  state.file_state.files.back().dot.r.p1 = command_window.size();
  state.file_state.files.back().dot.r.p2 = command_window.size();
  state.file_state.files.back().enc = jamlib::ENC_UTF8;
  state.file_id_to_window_id.push_back((uint32_t)state.windows.size());
  state.file_state.files.emplace_back();
  state.file_state.files.back().dot.r.p1 = 0;
  state.file_state.files.back().dot.r.p2 = 0;
  state.file_state.files.back().enc = jamlib::ENC_UTF8;
  state.file_id_to_window_id.push_back((uint32_t)state.windows.size() + 1);
  state.file_state.active_file = state.file_state.files.size() - 1;

  state.windows.emplace_back(0, 0, icols, 1, (uint32_t)state.file_state.files.size() - 2, (uint32_t)state.file_state.files.size() - 1, true);
  state.windows.emplace_back(0, 1, icols, irows - 2, (uint32_t)state.file_state.files.size() - 1, (uint32_t)state.file_state.files.size() - 2, false);

  state.window_pairs.emplace_back(0, 0, icols, irows, (uint32_t)state.windows.size() - 1, (uint32_t)state.windows.size() - 2);

  column_item ci;
  ci.column_id = column_id;
  ci.top_layer = 0.0;
  ci.bottom_layer = 1.0;
  ci.window_pair_id = (uint32_t)(state.window_pairs.size() - 1);
  if (!state.g.columns[column_id].items.empty())
    {
    auto pos = state.g.columns[column_id].items.size();
    for (size_t k = 0; k < state.g.columns[column_id].items.size(); ++k) // look for window with file_id == id == active_file
      {
      if (state.windows[state.window_pairs[state.g.columns[column_id].items[k].window_pair_id].window_id].file_id == id)
        {
        state.g.columns[column_id].items[k].bottom_layer = (state.g.columns[column_id].items[k].bottom_layer + state.g.columns[column_id].items[k].top_layer)*0.5;
        ci.top_layer = state.g.columns[column_id].items[k].bottom_layer;
        pos = k + 1;
        break;
        }
      }

    state.g.columns[column_id].items.insert(state.g.columns[column_id].items.begin() + pos, ci);
    }
  else
    state.g.columns[column_id].items.push_back(ci);

  state.file_state.active_file = state.file_state.files.size() - 1;

  return optimize_column(state, state.file_state.files.size() - 1);
  }

std::wstring get_piped_prompt(jamlib::file f)
  {
  int64_t p2 = f.content.size();
  f.dot.r.p1 = f.dot.r.p2 = p2;
  int64_t p1 = get_begin_of_line(f);
  std::wstring out(f.content.begin() + p1, f.content.begin() + p2);
  return out;
  }

void split_command(std::string& first, std::string& remainder, const std::string& command);

app_state make_window_piped(app_state state, int64_t file_id, std::string win_command)
  {
  state.file_state.active_file = file_id;

  if (win_command.empty())
    win_command = "Cmd.exe";

  std::string executable_name;
  std::string folder;
  std::vector<std::string> parameters;
  jamlib::parse_command(executable_name, folder, parameters, win_command);
  auto path = folder + executable_name;
  char** argv = new char*[parameters.size() + 2];
  argv[0] = const_cast<char*>(path.c_str());
  for (int j = 0; j < parameters.size(); ++j)
    argv[j + 1] = const_cast<char*>(parameters[j].c_str());
  argv[parameters.size() + 1] = nullptr;  

  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  int err = 1;
  err = JAM::create_pipe(path.c_str(), argv, nullptr, &w.process);
  delete[] argv;
  if (err != 0)
    {
    std::stringstream str;
    str << "Could not create a piped window";
    return add_error_text(state, str.str());
    }

  uint32_t command_id = state.windows[state.file_id_to_window_id[file_id]].nephew_id;
  assert(command_id >= 0);

  state.file_state.files[file_id].filename = "+" + cleanup(win_command);
  state.file_state.files[command_id].filename = state.file_state.files[file_id].filename;

  state.file_state.files[command_id].content = jamlib::buffer();
  auto command_window = state.file_state.files[command_id].content.transient();
  auto text = make_command_text(state, command_id);
  for (auto ch : text)
    command_window.push_back(ch);
  state.file_state.files[command_id].content = command_window.persistent();
  state.file_state.files[command_id].dot.r.p1 = command_window.size();
  state.file_state.files[command_id].dot.r.p2 = command_window.size();
  state.file_state.files[command_id].enc = jamlib::ENC_UTF8;

  auto piped_window = state.file_state.files[file_id].content.transient();
  std::string piped_text = JAM::read_from_pipe(w.process, 100);
  std::wstring wpiped_text = JAM::convert_string_to_wstring(piped_text);
  wpiped_text.erase(std::remove(wpiped_text.begin(), wpiped_text.end(), '\r'), wpiped_text.end());
  for (auto ch : wpiped_text)
    piped_window.push_back(ch);
  state.file_state.files[file_id].content = piped_window.persistent();
  state.file_state.files[file_id].dot.r.p1 = piped_window.size();
  state.file_state.files[file_id].dot.r.p2 = piped_window.size();
  state.file_state.files[file_id].enc = jamlib::ENC_UTF8;
  w.piped = true;
  w.piped_prompt = get_piped_prompt(state.file_state.files[file_id]);
  w.file_pos = 0;
  w.file_col = 0;
  w.wordwrap_row = 0;
  return state;

  }

std::optional<app_state> win_command(app_state state, int64_t id, const std::string& cmd)
  {
  state = *new_window(state, state.file_state.active_file, cmd);
  std::string win_command = cleanup(cmd.substr(3));
  std::stringstream ss;
  while (!win_command.empty())
    {
    std::string first, rest;
    split_command(first, rest, win_command);
    auto par_path = get_file_path(first, state.file_state.files[id].filename);
    if (par_path.empty())
      ss << first << " ";
    else
      ss << par_path << " ";
    win_command = cleanup(rest);
    }

  uint32_t file_id = (uint32_t)(state.file_state.files.size() - 1);
  return make_window_piped(state, file_id, ss.str());
  }

app_state update_window_to_dot(app_state state)
  {
  auto& f = state.file_state.files[state.file_state.active_file];
  if (f.dot.r.p1 > f.content.size())
    f.dot.r.p1 = f.content.size();
  if (f.dot.r.p2 > f.content.size())
    f.dot.r.p2 = f.content.size();
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  if (w.file_pos > f.dot.r.p1) // commented out because of crash after command Edit,x/4103032956/s/4103032956/03/
    {
    w.file_col = 0;
    w.file_pos = get_line_begin(f, f.dot.r.p1);
    w.wordwrap_row = 0;
    }
  else
    {
    w.file_col = 0;
    w.file_pos = get_line_begin(f, w.file_pos);
    w.wordwrap_row = 0;
    }
  return check_boundaries(state, w.word_wrap);
  }

app_state update_window_file_pos(app_state state)
  {
  auto& f = state.file_state.files[state.file_state.active_file];
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  w.file_pos = get_line_begin(f, w.file_pos);
  assert(has_valid_file_pos(state));
  return state;
  }


bool should_update_corresponding_command_window(const app_state& state)
  {
  const auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  if (w.nephew_id != (uint32_t)-1)
    {
    if (this_is_a_command_window(state, w.nephew_id))
      {
      return should_update_command_text(state, w.nephew_id);
      }
    else if (w.is_command_window)
      {
      return should_update_command_text(state, w.file_id);
      }
    }
  return false;
  }

app_state update_corresponding_command_window(app_state state)
  {
  const auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  if (w.is_command_window)
    return update_command_text(state, w.file_id);
  else
    return update_command_text(state, w.nephew_id);
  }

std::string get_filename_from_command_tag(const app_state& state, uint32_t command_id)
  {
  assert(this_is_a_command_window(state, command_id));
  const auto& f = state.file_state.files[command_id];
  const wchar_t bar = (wchar_t)'|';
  auto it0 = std::find(f.content.begin(), f.content.end(), bar); // find first bar
  if (it0 == f.content.end())
    return "";
  std::wstring filename(f.content.begin(), it0);
  return jamlib::convert_wstring_to_string(filename, f.enc);
  }

bool should_update_filename(const app_state& state)
  {
  const auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  if (w.nephew_id != (uint32_t)-1)
    {
    uint32_t command_window_id = w.is_command_window ? w.file_id : w.nephew_id;
    const auto& f = state.file_state.files[command_window_id];
    std::string filename = cleanup(get_filename_from_command_tag(state, command_window_id));
    return f.filename != filename;
    }
  return false;
  }

app_state update_filename(app_state state)
  {
  const auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  uint32_t command_window_id = w.is_command_window ? w.file_id : w.nephew_id;
  std::string filename = flip_backslash_to_slash_in_filename(cleanup(get_filename_from_command_tag(state, command_window_id)));
  state.file_state.files[w.file_id].filename = filename;
  state.file_state.files[w.nephew_id].filename = filename;
  state.file_state.files[w.file_id].modification_mask |= 1;
  state.file_state.files[w.nephew_id].modification_mask |= 1;
  return state;
  }


jamlib::buffer get_folder_list(const std::string& foldername)
  {
  jamlib::buffer newfoldertextbuffer;
  auto folder_content = newfoldertextbuffer.transient();

  folder_content.push_back('.');
  folder_content.push_back('.');
  folder_content.push_back('\n');

  auto items = JAM::get_subdirectories_from_directory(foldername, false);

  for (auto& item : items)
    {
    item = JAM::get_filename(item);
    auto witem = JAM::convert_string_to_wstring(item);
    for (auto ch : witem)
      folder_content.push_back(ch);
    folder_content.push_back('/');
    folder_content.push_back('\n');
    }

  items = JAM::get_files_from_directory(foldername, false);

  for (auto& item : items)
    {
    item = JAM::get_filename(item);
    auto witem = JAM::convert_string_to_wstring(item);
    for (auto ch : witem)
      folder_content.push_back(ch);
    folder_content.push_back('\n');
    }

  return folder_content.persistent();
  }

std::optional<app_state> get_command(app_state state, int64_t id, const std::string&)
  {
  auto& w = state.windows[state.file_id_to_window_id[get_active_file_id(state, id)]];
  if (w.nephew_id == (uint32_t)-1)
    return state;
  auto& f = state.file_state.files[get_active_file_id(state, id)];

  if (JAM::is_directory(f.filename))
    f.content = get_folder_list(f.filename);

  if (JAM::file_exists(f.filename))
    {
    state.file_state.active_file = get_active_file_id(state, id);
    std::stringstream load_file_command;
    load_file_command << ",r " << f.filename;
    state.file_state = *jamlib::handle_command(state.file_state, load_file_command.str());
    }

  if (!has_valid_file_pos(w, state))
    w.file_pos = 0;

  return state;
  }

std::optional<app_state> put_command(app_state state, int64_t id, const std::string&)
  {
  auto& w = state.windows[state.file_id_to_window_id[get_active_file_id(state, id)]];
  if (w.nephew_id == (uint32_t)-1)
    return state;
  auto& f = state.file_state.files[w.file_id];
  assert(f.filename == state.file_state.files[w.nephew_id].filename);
  state.file_state.active_file = w.file_id;
  std::stringstream str;
  str << "w " << f.filename;
  state.file_state = *jamlib::handle_command(state.file_state, str.str());
  return state;
  }

std::optional<app_state> putall_command(app_state state, int64_t id, const std::string& cmd)
  {
  size_t nr_win = state.windows.size();
  for (size_t j = 0; j < nr_win; ++j)
    {
    auto& w = state.windows[j];
    if (!w.is_command_window)
      {
      uint32_t file_id = w.file_id;
      const auto& f = state.file_state.files[file_id];
      if (is_modified(f))
        state = *put_command(state, file_id, cmd);
      }
    }
  return state;
  }

std::optional<app_state> highlight_comments_command(app_state state, int64_t id, const std::string&)
  {
  auto& w = state.windows[state.file_id_to_window_id[get_active_file_id(state, id)]];
  w.highlight_comments = !w.highlight_comments;
  return state;
  }

std::optional<app_state> wrap_command(app_state state, int64_t id, const std::string&)
  {
  auto& w = state.windows[state.file_id_to_window_id[get_active_file_id(state, id)]];
  w.wordwrap_row = 0;
  w.word_wrap = !w.word_wrap;
  return state;
  }

std::optional<app_state> utf8_command(app_state state, int64_t id, const std::string&)
  {
  state.file_state.active_file = get_active_file_id(state, id);
  state.file_state = *jamlib::handle_command(state.file_state, "U");
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);
  w.file_pos = w.file_col = 0;
  w.wordwrap_row = 0;
  return state;
  }

std::optional<app_state> ascii_command(app_state state, int64_t id, const std::string&)
  {
  state.file_state.active_file = get_active_file_id(state, id);
  state.file_state = *jamlib::handle_command(state.file_state, "A");
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);
  w.file_pos = w.file_col = 0;
  w.wordwrap_row = 0;
  return state;
  }


std::optional<app_state> dejavu_command(app_state state, int64_t, const std::string&)
  {
  gp_settings->font = JAM::get_folder(JAM::get_executable_path()) + "Font/DejaVuSansMono.ttf";
  return resize_font(state, gp_settings->font_size);
  }

std::optional<app_state> hack_command(app_state state, int64_t, const std::string&)
  {
  gp_settings->font = JAM::get_folder(JAM::get_executable_path()) + "Font/Hack-Regular.ttf";
  return resize_font(state, gp_settings->font_size);
  }

std::optional<app_state> noto_command(app_state state, int64_t, const std::string&)
  {
  gp_settings->font = JAM::get_folder(JAM::get_executable_path()) + "Font/NotoMono-Regular.ttf";
  return resize_font(state, gp_settings->font_size);
  }

std::optional<app_state> tab_command(app_state state, int64_t, const std::string&)
  {
  gp_settings->use_spaces_for_tab = !gp_settings->use_spaces_for_tab;
  return state;
  }

std::optional<app_state> consola_command(app_state state, int64_t, const std::string&)
  {
  gp_settings->font = "C:/Windows/Fonts/consola.ttf";
  return resize_font(state, gp_settings->font_size);
  }

std::optional<app_state> edit_command(app_state state, int64_t id, const std::string& cmd)
  {
  state.file_state.active_file = get_active_file_id(state, id);
  auto history_size = state.file_state.files[state.file_state.active_file].history.size();
  auto dot = state.file_state.files[state.file_state.active_file].dot;
  std::string edit_command = cmd.substr(4);
  try
    {
    state.file_state = *jamlib::handle_command(state.file_state, edit_command);
    }
  catch (std::runtime_error e)
    {
    state = add_error_text(state, e.what());
    }
  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);
  auto& f = state.file_state.files[state.file_state.active_file];
  if (f.history.size() == history_size + 1)
    {
    jamlib::snapshot ss = f.history.back();
    ss.dot = dot;
    f.history = f.history.set(f.history.size() - 1, ss);
    }
  return update_window_to_dot(state);
  }

std::optional<app_state> redo_command(app_state state, int64_t id, const std::string&)
  {
  state.file_state.active_file = get_active_file_id(state, id);
  state.file_state = *jamlib::handle_command(state.file_state, "R");
  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);
  return update_window_file_pos(state);
  }

std::optional<app_state> undo_command(app_state state, int64_t id, const std::string&)
  {
  state.file_state.active_file = get_active_file_id(state, id);
  state.file_state = *jamlib::handle_command(state.file_state, "u");
  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);
  return update_window_file_pos(state);
  }

std::optional<app_state> local_redo_command(app_state state, int64_t id, const std::string&)
  {
  state.file_state.active_file = id;
  state.file_state = *jamlib::handle_command(state.file_state, "R");
  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);
  return update_window_file_pos(state);
  }

std::optional<app_state> local_undo_command(app_state state, int64_t id, const std::string&)
  {
  state.file_state.active_file = id;
  state.file_state = *jamlib::handle_command(state.file_state, "u");
  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);
  return update_window_file_pos(state);
  }

std::optional<app_state> dostheme_command(app_state state, int64_t id, const std::string&)
  {
  gp_settings->win_bg_red = 0;
  gp_settings->win_bg_green = 0;
  gp_settings->win_bg_blue = 255;
  gp_settings->tag_bg_red = 0;
  gp_settings->tag_bg_green = 255;
  gp_settings->tag_bg_blue = 255;
  gp_settings->scrb_red = 255;
  gp_settings->scrb_green = 255;
  gp_settings->scrb_blue = 255;
  gp_settings->text_red = 255;
  gp_settings->text_green = 255;
  gp_settings->text_blue = 255;
  gp_settings->tag_text_red = 0;
  gp_settings->tag_text_green = 0;
  gp_settings->tag_text_blue = 0;
  gp_settings->middle_red = 255;
  gp_settings->middle_green = 0;
  gp_settings->middle_blue = 0;
  gp_settings->right_red = 255;
  gp_settings->right_green = 255;
  gp_settings->right_blue = 0;

  gp_settings->selection_red = 0;
  gp_settings->selection_green = 0;
  gp_settings->selection_blue = 0;
  gp_settings->selection_tag_red = 0;
  gp_settings->selection_tag_green = 0;
  gp_settings->selection_tag_blue = 0;

  init_colors(*gp_settings);

  stdscr->_clear = TRUE;

  SDL_Rect dest;

  dest.x = 0;
  dest.y = 0;
  dest.w = state.w;
  dest.h = state.h;

  SDL_FillRect(pdc_screen, &dest, SDL_MapRGB(pdc_screen->format, (uint8_t)gp_settings->win_bg_red, (uint8_t)gp_settings->win_bg_green, (uint8_t)gp_settings->win_bg_blue));


  return state;
  }

std::optional<app_state> darktheme_command(app_state state, int64_t id, const std::string&)
  {
  gp_settings->win_bg_red = 40;
  gp_settings->win_bg_green = 40;
  gp_settings->win_bg_blue = 40;
  gp_settings->tag_bg_red = 80;
  gp_settings->tag_bg_green = 80;
  gp_settings->tag_bg_blue = 80;
  gp_settings->scrb_red = 85;
  gp_settings->scrb_green = 130;
  gp_settings->scrb_blue = 241;
  gp_settings->text_red = 200;
  gp_settings->text_green = 200;
  gp_settings->text_blue = 200;
  gp_settings->tag_text_red = 200;
  gp_settings->tag_text_green = 200;
  gp_settings->tag_text_blue = 200;

  gp_settings->middle_red = 219; //219
  gp_settings->middle_green = 100; //100
  gp_settings->middle_blue = 100; //100

  gp_settings->right_red = 133; // 133
  gp_settings->right_green = 195; //195
  gp_settings->right_blue = 100; //100

  gp_settings->selection_red = 85; //85
  gp_settings->selection_green = 130; //130
  gp_settings->selection_blue = 241; // 241

  gp_settings->selection_tag_red = 85;
  gp_settings->selection_tag_green = 130;
  gp_settings->selection_tag_blue = 241;

  init_colors(*gp_settings);

  stdscr->_clear = TRUE;

  SDL_Rect dest;

  dest.x = 0;
  dest.y = 0;
  dest.w = state.w;
  dest.h = state.h;

  SDL_FillRect(pdc_screen, &dest, SDL_MapRGB(pdc_screen->format, (uint8_t)gp_settings->win_bg_red, (uint8_t)gp_settings->win_bg_green, (uint8_t)gp_settings->win_bg_blue));


  return state;
  }

std::optional<app_state> lotustheme_command(app_state state, int64_t id, const std::string&)
  {
  gp_settings->win_bg_red = 0;
  gp_settings->win_bg_green = 0;
  gp_settings->win_bg_blue = 0;
  gp_settings->tag_bg_red = 0;
  gp_settings->tag_bg_green = 0;
  gp_settings->tag_bg_blue = 200;
  gp_settings->scrb_red = 255;
  gp_settings->scrb_green = 255;
  gp_settings->scrb_blue = 0;
  gp_settings->text_red = 0;
  gp_settings->text_green = 200;
  gp_settings->text_blue = 0;
  gp_settings->tag_text_red = 255;
  gp_settings->tag_text_green = 255;
  gp_settings->tag_text_blue = 255;

  gp_settings->middle_red = 200;
  gp_settings->middle_green = 0;
  gp_settings->middle_blue = 0;

  gp_settings->right_red = 0;
  gp_settings->right_green = 255;
  gp_settings->right_blue = 0;

  gp_settings->selection_red = 255;
  gp_settings->selection_green = 255;
  gp_settings->selection_blue = 0;

  gp_settings->selection_tag_red = 255;
  gp_settings->selection_tag_green = 255;
  gp_settings->selection_tag_blue = 0;

  init_colors(*gp_settings);

  stdscr->_clear = TRUE;

  SDL_Rect dest;

  dest.x = 0;
  dest.y = 0;
  dest.w = state.w;
  dest.h = state.h;

  SDL_FillRect(pdc_screen, &dest, SDL_MapRGB(pdc_screen->format, (uint8_t)gp_settings->win_bg_red, (uint8_t)gp_settings->win_bg_green, (uint8_t)gp_settings->win_bg_blue));


  return state;
  }

std::optional<app_state> lighttheme_command(app_state state, int64_t id, const std::string&)
  {
  gp_settings->win_bg_red = 255;
  gp_settings->win_bg_green = 255;
  gp_settings->win_bg_blue = 240;
  gp_settings->tag_bg_red = 231;
  gp_settings->tag_bg_green = 251;
  gp_settings->tag_bg_blue = 252;
  gp_settings->scrb_red = 235;
  gp_settings->scrb_green = 233;
  gp_settings->scrb_blue = 162;
  gp_settings->text_red = 0;
  gp_settings->text_green = 0;
  gp_settings->text_blue = 0;
  gp_settings->tag_text_red = 0;
  gp_settings->tag_text_green = 0;
  gp_settings->tag_text_blue = 0;
  gp_settings->middle_red = 174;
  gp_settings->middle_green = 4;
  gp_settings->middle_blue = 17;
  gp_settings->right_red = 6;
  gp_settings->right_green = 98;
  gp_settings->right_blue = 3;
  gp_settings->selection_red = 235;
  gp_settings->selection_green = 233;
  gp_settings->selection_blue = 162;
  gp_settings->selection_tag_red = 158;
  gp_settings->selection_tag_green = 235;
  gp_settings->selection_tag_blue = 239;

  init_colors(*gp_settings);

  stdscr->_clear = TRUE;

  SDL_Rect dest;

  dest.x = 0;
  dest.y = 0;
  dest.w = state.w;
  dest.h = state.h;


  SDL_FillRect(pdc_screen, &dest, SDL_MapRGB(pdc_screen->format, (uint8_t)gp_settings->win_bg_red, (uint8_t)gp_settings->win_bg_green, (uint8_t)gp_settings->win_bg_blue));

  return state;
  }

app_state paste_from_snarf_buffer(app_state state);
app_state copy_to_snarf_buffer(app_state state);

std::optional<app_state> cut_command(app_state state, int64_t id, const std::string&)
  {
  state = copy_to_snarf_buffer(state);

  auto& f = state.file_state.files[state.file_state.active_file];

  jamlib::snapshot ss;
  ss.content = f.content;
  ss.dot = f.dot;
  ss.modification_mask = f.modification_mask;
  ss.enc = f.enc;

  if (f.dot.r.p1 != f.dot.r.p2)
    {
    f.content = f.content.erase((uint32_t)f.dot.r.p1, (uint32_t)f.dot.r.p2);
    f.dot.r.p2 = f.dot.r.p1;
    f.modification_mask |= 1;
    f.history = f.history.push_back(ss);
    f.undo_redo_index = f.history.size();
    }

  return state;
  }

std::optional<app_state> paste_command(app_state state, int64_t id, const std::string&)
  {
  return paste_from_snarf_buffer(state);
  }

std::optional<app_state> snarf_command(app_state state, int64_t id, const std::string&)
  {
  return copy_to_snarf_buffer(state);
  }

const auto executable_commands = std::map<std::string, std::function<std::optional<app_state>(app_state, int64_t, const std::string&)>>
  {
      {"Cut", cut_command},
      {"Darktheme", darktheme_command},
      {"Del", del_window},
      {"Delcol", del_column},
      {"Dostheme", dostheme_command},
      {"Exit", exit_command},
      {"Edit", edit_command},
      {"Get", get_command},
      {"Lighttheme", lighttheme_command},
      {"Lotustheme", lotustheme_command},
      {"New", new_window},
      {"Newcol", new_column_command},
      {"Comment", highlight_comments_command},
      {"Paste", paste_command},
      {"Put", put_command},
      {"Putall", putall_command},
      {"Redo", redo_command},
      {"Snarf", snarf_command},
      {"Undo", undo_command},
      {"Win", win_command},
      {"Wrap", wrap_command},
      {"UTF8", utf8_command},
      {"Ascii", ascii_command},
      {"Dejavu", dejavu_command},
      {"Hack", hack_command},
      {"Noto", noto_command},
      {"Consola", consola_command},
      {"Tab", tab_command}
  };

std::string get_command(const app_state& state, int64_t p1, int64_t p2, int64_t id)
  {
  const auto& f = state.file_state.files[id];
  if (p1 == p2)
    {
    if (p1 >= f.dot.r.p1 && p2 <= f.dot.r.p2 && f.dot.r.p1 != f.dot.r.p2)
      {
      std::string str = cleanup(get_utf8_string(f.content.begin() + f.dot.r.p1, f.content.begin() + f.dot.r.p2, f.enc));
      return str;
      }

    if (p2 > state.file_state.files[id].content.size())
      {
      p2 = state.file_state.files[id].content.size();
      p1 = p2;
      }
    auto it0 = state.file_state.files[id].content.begin();
    auto it = state.file_state.files[id].content.begin() + p1;
    auto it2 = it;
    auto it_end = state.file_state.files[id].content.end();
    if (it0 == it_end)
      return "";
    if (it == it_end)
      --it;
    while (it > it0)
      {
      if (*it == ' ' || *it == '\n' || *it == '\t' || *it == '"')
        break;
      --it;
      }
    if (*it == ' ' || *it == '\n' || *it == '\t' || *it == '"')
      ++it;
    while (it2 < it_end)
      {
      if (*it2 == ' ' || *it2 == '\n' || *it2 == '\t' || *it2 == '"')
        break;
      ++it2;
      }
    if (it2 <= it)
      return "";
    std::wstring wcmd(it, it2);
    return jamlib::convert_wstring_to_string(wcmd, state.file_state.files[id].enc);
    }
  if (p1 >= f.dot.r.p1 && p2 <= f.dot.r.p2)
    {
    std::string str = cleanup(get_utf8_string(f.content.begin() + f.dot.r.p1, f.content.begin() + f.dot.r.p2, f.enc));
    return str;
    }
  if (p2 > state.file_state.files[id].content.size())
    p2 = state.file_state.files[id].content.size();
  auto it = state.file_state.files[id].content.begin() + p1;
  auto it_end = state.file_state.files[id].content.begin() + p2;
  std::wstring wcmd(it, it_end);
  return cleanup(jamlib::convert_wstring_to_string(wcmd, state.file_state.files[id].enc));
  }

std::string get_first_word(const std::string& command)
  {
  auto pos = command.find_first_of(' ');
  if (pos == std::string::npos)
    return command;

  auto pos_quote = command.find_first_of('"');
  if (pos < pos_quote)
    return command.substr(0, pos);

  auto pos_quote_2 = pos_quote + 1;
  while (pos_quote_2 < command.size() && command[pos_quote_2] != '"')
    ++pos_quote_2;
  if (pos_quote_2 == command.size())
    return command;
  auto cmd = command.substr(0, pos_quote_2 + 1);
  cmd.erase(pos_quote_2, 1);
  cmd.erase(pos_quote, 1);
  return cmd;
  }

void split_command(std::string& first, std::string& remainder, const std::string& command)
  {
  first.clear();
  remainder.clear();
  auto pos = command.find_first_of(' ');
  if (pos == std::string::npos)
    {
    first = command;
    return;
    }

  auto pos_quote = command.find_first_of('"');
  if (pos < pos_quote)
    {
    first = command.substr(0, pos);
    remainder = command.substr(pos);
    return;
    }

  auto pos_quote_2 = pos_quote + 1;
  while (pos_quote_2 < command.size() && command[pos_quote_2] != '"')
    ++pos_quote_2;
  if (pos_quote_2+1 == command.size())
    {
    first = command;
    return;
    }
  first = command.substr(0, pos_quote_2 + 1);
  //first.erase(pos_quote_2, 1);
  //first.erase(pos_quote, 1);
  remainder = command.substr(pos_quote_2 + 1);
  }


std::optional<app_state> execute(app_state state, int64_t p1, int64_t p2, int64_t id)
  {
  if (id < 0 || p1 < 0 || p2 < 0)
    return state;
  auto cmd = get_command(state, p1, p2, id);
  if (cmd.empty())
    return state;

  if (state.windows[state.file_id_to_window_id[id]].piped) // if we execute in a piped window
    {
    std::string text;
    auto& w = state.windows[state.file_id_to_window_id[id]];
    auto& f = state.file_state.files[id];
    try
      {
      cmd.push_back('\n');
      JAM::send_to_pipe(w.process, cmd.c_str());
      text = JAM::read_from_pipe(w.process, 100);
      }
    catch (std::runtime_error e)
      {
      w.piped = false;
      w.process = nullptr;
      state = add_error_text(state, e.what());
      return state;
      }

    jamlib::snapshot ss;
    ss.content = f.content;
    ss.dot = f.dot;
    ss.modification_mask = f.modification_mask;
    ss.enc = f.enc;

    jamlib::buffer txt;
    auto tr = txt.transient();
    auto wtext = convert_string_to_wstring(text, f.enc);
    wtext.erase(std::remove(wtext.begin(), wtext.end(), '\r'), wtext.end());
    tr.push_back('\n');
    for (auto ch : wtext)
      {
      tr.push_back(ch);
      }
    txt = tr.persistent();
    f.content = f.content.insert((uint32_t)f.content.size(), txt);
    f.dot.r.p1 = f.content.size();
    f.dot.r.p2 = f.content.size();
    f.modification_mask |= 1;
    f.history = f.history.push_back(ss);
    f.undo_redo_index = f.history.size();
    w.file_pos = get_line_begin(f, w.file_pos);
    w.piped_prompt = get_piped_prompt(f);
    if (should_update_corresponding_command_window(state))
      state = update_corresponding_command_window(state);
    return check_boundaries(state, w.word_wrap);
    }

  //auto cmd_id = get_first_word(cmd);
  std::string cmd_id, cmd_remainder;
  split_command(cmd_id, cmd_remainder, cmd);

  auto it = executable_commands.find(cmd_id);
  if (it != executable_commands.end())
    {
    return it->second(state, id, cmd);
    }
  if (cmd.substr(0, 4) == "Edit")
    return edit_command(state, id, cmd);
  if (cmd.substr(0, 3) == "Win")
    return win_command(state, id, cmd);

  char pipe_cmd = cmd_id[0];
  if (pipe_cmd == '!' || pipe_cmd == '<' || pipe_cmd == '>' || pipe_cmd == '|')
    {
    cmd_id.erase(cmd_id.begin());
    cmd.erase(cmd.begin());
    }
  else
    pipe_cmd = '!';

  //auto window_folder = get_folder(state.file_state.files[id].filename);

  auto file_path = get_file_path(cmd_id, state.file_state.files[id].filename);

  if (file_path.empty())
    return state;

  std::vector<std::string> parameters;
  while (!cmd_remainder.empty())
    {
    cmd_remainder = cleanup(cmd_remainder);
    std::string first, rest;
    split_command(first, rest, cmd_remainder);
    auto par_path = get_file_path(first, state.file_state.files[id].filename);
    if (par_path.empty())
      parameters.push_back(first);
    else
      parameters.push_back(par_path);
    cmd_remainder = cleanup(rest);
    }

  std::stringstream ss;
  ss << pipe_cmd << file_path;
  for (const auto& p : parameters)
    ss << " " << p;
  try
    {
    if (id != get_active_file_id(state, id))
      {
      state.file_state.active_file = get_active_file_id(state, id);
      }
    JAM::active_folder af(JAM::get_folder(state.file_state.files[id].filename).c_str());
    state.file_state = *jamlib::handle_command(state.file_state, ss.str());
    auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
    w.file_pos = get_line_begin(state.file_state.files[state.file_state.active_file], w.file_pos);
    assert(has_valid_file_pos(state));
    if (should_update_corresponding_command_window(state))
      state = update_corresponding_command_window(state);
    }
  catch (std::runtime_error e)
    {
    state = add_error_text(state, e.what());
    }
  return state;
  /*

  if (!state.file_state.files[id].filename.empty())
    {
    auto possible_executables = get_files_from_directory(window_folder, false);
    for (const auto& path : possible_executables)
      {
      auto filename = get_filename(path);
      if (filename == cmd_id || remove_extension(filename) == cmd_id)
        {
        std::stringstream ss;
        ss << pipe_cmd << window_folder << cmd;
        try
          {
          if (state.file_state.active_file == id && id != get_active_file_id(state, id))
            {
            state.file_state.active_file = get_active_file_id(state, id);
            }
          active_folder af(window_folder);
          state.file_state = *jamlib::handle_command(state.file_state, ss.str());
          auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
          w.file_pos = get_line_begin(state.file_state.files[state.file_state.active_file], w.file_pos);
          assert(has_valid_file_pos(state));
          if (should_update_corresponding_command_window(state))
            state = update_corresponding_command_window(state);
          }
        catch (std::runtime_error e)
          {
          state = add_error_text(state, e.what());
          }
        return state;
        }
      }
    }

  auto executable_path = get_folder(get_executable_path());
  auto possible_executables = get_files_from_directory(executable_path, false);
  for (const auto& path : possible_executables)
    {
    auto filename = get_filename(path);
    if (filename == cmd_id || remove_extension(filename) == cmd_id)
      {
      std::stringstream ss;
      ss << pipe_cmd << executable_path << cmd;
      try
        {
        if (state.file_state.active_file == id && id != get_active_file_id(state, id))
          {
          state.file_state.active_file = get_active_file_id(state, id);
          }
        active_folder af(window_folder);
        state.file_state = *jamlib::handle_command(state.file_state, ss.str());
        auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
        w.file_pos = get_line_begin(state.file_state.files[state.file_state.active_file], w.file_pos);
        assert(has_valid_file_pos(state));
        if (should_update_corresponding_command_window(state))
          state = update_corresponding_command_window(state);
        }
      catch (std::runtime_error e)
        {
        state = add_error_text(state, e.what());
        }
      return state;
      }
    }

  if (file_exists(cmd_id))
    {
    std::stringstream ss;
    ss << pipe_cmd << cmd;
    try
      {
      if (state.file_state.active_file == id && id != get_active_file_id(state, id))
        {
        state.file_state.active_file = get_active_file_id(state, id);
        }
      active_folder af(window_folder);
      state.file_state = *jamlib::handle_command(state.file_state, ss.str());
      auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
      w.file_pos = get_line_begin(state.file_state.files[state.file_state.active_file], w.file_pos);
      assert(has_valid_file_pos(state));
      if (should_update_corresponding_command_window(state))
        state = update_corresponding_command_window(state);
      }
    catch (std::runtime_error e)
      {
      state = add_error_text(state, e.what());
      }
    return state;
    }
    */
  return state;
  }

uint32_t get_empty_column_id(const app_state& state)
  {
  for (uint32_t i = 0; i < state.g.columns.size(); ++i)
    {
    if (state.g.columns[i].items.empty())
      return i;
    }
  return (uint32_t)-1;
  }

uint32_t get_column_id(const app_state& state, int64_t id)
  {
  for (uint32_t i = 0; i < state.g.columns.size(); ++i)
    {
    auto& c = state.g.columns[i];
    if (state.windows[c.column_command_window_id].file_id == id)
      return i;
    for (uint32_t j = 0; j < c.items.size(); ++j)
      {
      auto ci = c.items[j];
      auto& wp = state.window_pairs[ci.window_pair_id];
      if (state.windows[wp.command_window_id].file_id == id || state.windows[wp.window_id].file_id == id)
        {
        return i;
        }
      }
    }
  return (uint32_t)state.g.columns.size() - (uint32_t)1;
  }

std::optional<app_state> load_file(std::string filename, app_state state, int64_t id)
  {
  filename = flip_backslash_to_slash_in_filename(filename);

  if (filename.find(' ') != std::string::npos)
    {
    filename.push_back('"');
    filename.insert(filename.begin(), '"');
    }

  if (state.g.columns.empty())
    state = *new_column_command(state, 0, "");


  uint32_t column_id = get_empty_column_id(state);
  if (column_id == (uint32_t)-1)
    column_id = get_column_id(state, id);

  assert(!state.g.columns.empty());

  int icols = get_cols();
  int irows = get_lines();

  state.file_state.files.emplace_back();
  auto command_window = state.file_state.files.back().content.transient();
  state.file_state.files.back().filename = filename;
  //std::string text = filename + " " + gp_settings->command_text;
  auto text = make_command_text(state, state.file_state.files.size() - 1);
  for (auto ch : text)
    command_window.push_back(ch);
  state.file_state.files.back().content = command_window.persistent();
  state.file_state.files.back().dot.r.p1 = command_window.size();
  state.file_state.files.back().dot.r.p2 = command_window.size();
  state.file_state.files.back().enc = jamlib::ENC_UTF8;
  state.file_id_to_window_id.push_back((uint32_t)state.windows.size());
  state.file_state.files.emplace_back();


  state.file_state.active_file = state.file_state.files.size() - 1;

  std::stringstream load_file_command;
  load_file_command << "r " << filename;
  state.file_state.files.back().enc = jamlib::ENC_UTF8;
  state.file_state = *jamlib::handle_command(state.file_state, load_file_command.str());
  state.file_state.files.back().modification_mask = 0;
  state.file_state.files.back().history = immutable::vector<jamlib::snapshot, false>();
  state.file_state.files.back().undo_redo_index = 0;
  state.file_state.files.back().dot.r.p1 = 0;
  state.file_state.files.back().dot.r.p2 = 0;
  state.file_state.files.back().filename = filename;
  state.file_id_to_window_id.push_back((uint32_t)state.windows.size() + 1);

  state.windows.emplace_back(0, 0, icols, 1, (uint32_t)state.file_state.files.size() - 2, (uint32_t)state.file_state.files.size() - 1, true);
  state.windows.emplace_back(0, 1, icols, irows - 2, (uint32_t)state.file_state.files.size() - 1, (uint32_t)state.file_state.files.size() - 2, false);

  state.window_pairs.emplace_back(0, 0, icols, irows, (uint32_t)state.windows.size() - 1, (uint32_t)state.windows.size() - 2);

  column_item ci;
  ci.column_id = column_id;
  ci.top_layer = 0.0;
  ci.bottom_layer = 1.0;
  ci.window_pair_id = (uint32_t)(state.window_pairs.size() - 1);
  if (!state.g.columns[column_id].items.empty())
    {
    auto pos = state.g.columns[column_id].items.size();
    for (size_t k = 0; k < state.g.columns[column_id].items.size(); ++k) // look for window with file_id == id == active_file
      {
      if (state.windows[state.window_pairs[state.g.columns[column_id].items[k].window_pair_id].window_id].file_id == id)
        {
        state.g.columns[column_id].items[k].bottom_layer = (state.g.columns[column_id].items[k].bottom_layer + state.g.columns[column_id].items[k].top_layer)*0.5;
        ci.top_layer = state.g.columns[column_id].items[k].bottom_layer;
        pos = k + 1;
        break;
        }
      }
    state.g.columns[column_id].items.insert(state.g.columns[column_id].items.begin() + pos, ci);
    }
  else
    state.g.columns[column_id].items.push_back(ci);

  return optimize_column(state, state.file_state.files.size() - 1);
  }

std::optional<app_state> load_folder(std::string foldername, app_state state, int64_t id)
  {
  foldername = flip_backslash_to_slash_in_filename(cleanup_foldername(foldername));

  if (state.g.columns.empty())
    state = *new_column_command(state, 0, "");

  assert(!state.g.columns.empty());

  int icols = get_cols();
  int irows = get_lines();

  if (id >= 0 && !state.file_state.files[id].filename.empty() && JAM::is_directory(state.file_state.files[id].filename))
    {
    auto path = state.file_state.files[id].filename;
    id = get_active_file_id(state, id);
    auto& f = state.file_state.files[id];
    f.modification_mask = 0;
    f.history = immutable::vector<jamlib::snapshot, false>();
    f.undo_redo_index = 0;
    f.filename = foldername;
    auto& w = state.windows[state.file_id_to_window_id[id]];
    auto& tag = state.file_state.files[w.nephew_id];
    tag.filename = foldername;
    auto tagtext = make_command_text(state, w.nephew_id);
    jamlib::buffer newtagtextbuffer;
    auto t = newtagtextbuffer.transient();
    for (auto ch : tagtext)
      t.push_back(ch);
    tag.content = t.persistent();
    tag.dot.r.p1 = tag.dot.r.p2 = tag.content.size();
    f.content = get_folder_list(foldername);
    f.dot.r.p1 = f.dot.r.p2 = 0;

    w.file_pos = 0;

    if (should_update_corresponding_command_window(state))
      state = update_corresponding_command_window(state);

    return state;
    }
  else
    {

    state.file_state.files.emplace_back();
    auto command_window = state.file_state.files.back().content.transient();
    state.file_state.files.back().filename = foldername;
    //std::string text = foldername + " " + gp_settings->command_text;
    auto text = make_command_text(state, state.file_state.files.size() - 1);
    for (auto ch : text)
      command_window.push_back(ch);
    state.file_state.files.back().content = command_window.persistent();
    state.file_state.files.back().dot.r.p1 = command_window.size();
    state.file_state.files.back().dot.r.p2 = command_window.size();
    state.file_state.files.back().enc = jamlib::ENC_UTF8;
    state.file_id_to_window_id.push_back((uint32_t)state.windows.size());
    state.file_state.files.emplace_back();

    state.file_state.files.back().enc = jamlib::ENC_UTF8;
    state.file_state.files.back().content = get_folder_list(foldername);


    state.file_state.active_file = state.file_state.files.size() - 1;


    state.file_state.files.back().modification_mask = 0;
    state.file_state.files.back().dot.r.p1 = 0;
    state.file_state.files.back().dot.r.p2 = 0;
    state.file_state.files.back().filename = foldername;
    state.file_id_to_window_id.push_back((uint32_t)state.windows.size() + 1);

    state.windows.emplace_back(0, 0, icols, 1, (uint32_t)state.file_state.files.size() - 2, (uint32_t)state.file_state.files.size() - 1, true);
    state.windows.emplace_back(0, 1, icols, irows - 2, (uint32_t)state.file_state.files.size() - 1, (uint32_t)state.file_state.files.size() - 2, false);

    state.window_pairs.emplace_back(0, 0, icols, irows, (uint32_t)state.windows.size() - 1, (uint32_t)state.windows.size() - 2);

    column_item ci;
    ci.column_id = state.g.columns.size() - 1;
    ci.top_layer = 0.0;
    ci.bottom_layer = 1.0;
    ci.window_pair_id = (uint32_t)(state.window_pairs.size() - 1);
    if (!state.g.columns.back().items.empty())
      {
      state.g.columns.back().items.back().bottom_layer = (state.g.columns.back().items.back().bottom_layer + state.g.columns.back().items.back().top_layer)*0.5;
      ci.top_layer = state.g.columns.back().items.back().bottom_layer;
      }
    state.g.columns.back().items.push_back(ci);

    if (should_update_corresponding_command_window(state))
      state = update_corresponding_command_window(state);

    return optimize_column(state, state.file_state.files.size() - 1);
    }
  }

std::optional<app_state> load(app_state state, int64_t p1, int64_t p2, int64_t id)
  {
  if (id < 0 || p1 < 0 || p2 < 0)
    return state;
  auto cmd = get_command(state, p1, p2, id);
  if (cmd.empty())
    return state;

  std::string filename = state.file_state.files[get_active_file_id(state, id)].filename;
  std::string folder = JAM::get_folder(filename);

  if (folder.empty())
    folder = JAM::get_executable_path();

  if (folder.back() != '\\' && folder.back() != '/')
    folder.push_back('/');
  std::string newfilename = folder + cmd;

  if (JAM::file_exists(newfilename))
    return load_file(newfilename, state, state.file_state.active_file);

  if (JAM::file_exists(cmd))
    return load_file(cmd, state, state.file_state.active_file);

  if (JAM::is_directory(newfilename))
    {
    if (cmd == "..")
      {
      newfilename = folder;
      newfilename.pop_back();
      if (newfilename.find('/') == std::string::npos && newfilename.find('\\') == std::string::npos)
        newfilename = folder;
      newfilename = JAM::get_folder(newfilename);
      if (newfilename.back() != '\\' && newfilename.back() != '/')
        newfilename.push_back('/');
      if (newfilename.back() == '\\')
        newfilename.back() = '/';
      }
    return load_folder(newfilename, state, id);
    }

  if (JAM::is_directory(cmd))
    return load_folder(cmd, state, id);

  state.file_state.active_file = get_active_file_id(state, id);
  std::stringstream find_str;
  bool fullsearch = false;
  if (state.file_state.files[state.file_state.active_file].dot.r.p1 == (state.file_state.files[state.file_state.active_file].content.size()))
    fullsearch = true;
  if (fullsearch)
    find_str << "0+/(" << resolve_jamlib_escape_characters(resolve_regex_escape_characters(cmd)) << ")/";
  else
    find_str << ".+/(" << resolve_jamlib_escape_characters(resolve_regex_escape_characters(cmd)) << ")/";
  try
    {
    state.file_state = *jamlib::handle_command(state.file_state, find_str.str());
    if (!fullsearch && state.file_state.files[state.file_state.active_file].dot.r.p1 == (state.file_state.files[state.file_state.active_file].content.size()))
      {
      std::stringstream find_str_2;
      find_str_2 << "0+/(" << resolve_jamlib_escape_characters(resolve_regex_escape_characters(cmd)) << ")/";
      state.file_state = *jamlib::handle_command(state.file_state, find_str_2.str());
      }
    }
  catch (std::runtime_error e)
    {
    return add_error_text(state, e.what());
    }
  return update_window_to_dot(state);
  }

bool invalid_command_window_position(const app_state& state)
  {
  const auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  if (!w.is_command_window)
    return false;
  if (w.nephew_id == (uint32_t)-1)
    return false;

  auto dot = find_command_window_fixed_dot(state, w.file_id);

  const auto& f = state.file_state.files[w.file_id];

  if (f.dot.r.p1 > dot.r.p1 && f.dot.r.p1 <= dot.r.p2)
    return true;
  if (f.dot.r.p2 > dot.r.p1 && f.dot.r.p2 <= dot.r.p2)
    return true;

  return false;
  }

app_state backspace(app_state state)
  {
  if (invalid_command_window_position(state))
    return state;
  auto& d = state.file_state.files[state.file_state.active_file].dot;
  if (d.r.p1 == d.r.p2)
    {
    if (d.r.p1 > 0)
      --d.r.p1;
    }
  state.file_state = *jamlib::handle_command(state.file_state, "d");

  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  w.file_pos = get_line_begin(state.file_state.files[state.file_state.active_file], w.file_pos);

  if (should_update_filename(state))
    state = update_filename(state);

  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);

  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;
  return check_boundaries(state, word_wrap);
  }

app_state select_all_command(app_state state)
  {
  std::string command;
  command.push_back(',');
  state.file_state = *jamlib::handle_command(state.file_state, command);
  return state;
  }

app_state delete_press(app_state state, bool shift)
  {
  if (invalid_command_window_position(state))
    return state;
  if (shift)
    state = copy_to_snarf_buffer(state);
  std::string command;
  command.push_back('d');
  auto& f = state.file_state.files[state.file_state.active_file];
  if (f.dot.r.p1 == f.dot.r.p2 && f.dot.r.p2 < f.content.size())
    ++f.dot.r.p2;
  if (f.dot.r.p1 == f.dot.r.p2)
    return state;
  state.file_state = *jamlib::handle_command(state.file_state, command);
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  w.file_pos = get_line_begin(state.file_state.files[state.file_state.active_file], w.file_pos);

  if (should_update_filename(state))
    state = update_filename(state);

  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);

  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;

  return check_boundaries(state, word_wrap);
  }

std::wstring resolve_piped_command_escape_characters(const std::wstring& cmd)
  {
  std::wstring out;
  out.reserve(cmd.size());
  for (auto ch : cmd)
    {
    switch (ch)
      {
      case '\\':
      {
      out.push_back('\\');
      out.push_back('\\');
      break;
      }
      case '\t':
      {
      out.push_back('\\');
      out.push_back('t');
      break;
      }
      case '\r':
      {
      out.push_back('\\');
      out.push_back('r');
      break;
      }
      case '\n':
      {
      out.push_back('\\');
      out.push_back('n');
      break;
      }
      default:
      {
      out.push_back(ch);
      break;
      }
      }
    }
  return out;
  }

std::string get_piped_command(const std::wstring& prompt, jamlib::file f)
  {
  int64_t p2 = f.content.size();
  f.dot.r.p1 = f.dot.r.p2 = p2;
  int64_t p1 = get_begin_of_line(f);
  std::wstring out(f.content.begin() + p1, f.content.begin() + p2);
  size_t find_prompt = out.find(prompt);
  if (find_prompt != std::wstring::npos)
    {
    out = out.substr(find_prompt + prompt.size());
    }
  out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
  out = resolve_piped_command_escape_characters(out);
  return jamlib::convert_wstring_to_string(out, f.enc);
  }

uint32_t get_history_index(const std::vector<std::string>& history, const std::string& piped_cmd)
  {
  auto it = std::find(history.begin(), history.end(), piped_cmd);
  if (it == history.end())
    return (uint32_t)-1;
  return (uint32_t)std::distance(history.begin(), it);
  }

app_state tab(app_state state)
  {
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  auto& f = state.file_state.files[state.file_state.active_file];
  auto& d = f.dot;
  int dot_jump_steps = 1;
  std::string command;
  if (d.r.p1 == d.r.p2)
    command.push_back('i');
  else
    {
    command.push_back('c');
    }
  command.push_back('/');

  if (gp_settings->use_spaces_for_tab)
    {
    dot_jump_steps = gp_settings->tab_space;
    for (int i = 0; i < dot_jump_steps; ++i)
      command.push_back(' ');
    }
  else
    {
    command.push_back('\\');
    command.push_back('t');
    }
  command.push_back('/');

  state.file_state = *jamlib::handle_command(state.file_state, command);
  state.file_state.files[state.file_state.active_file].dot.r.p1 += dot_jump_steps;
  state.file_state.files[state.file_state.active_file].dot.r.p2 = state.file_state.files[state.file_state.active_file].dot.r.p1;

  w.file_pos = get_line_begin(state.file_state.files[state.file_state.active_file], w.file_pos);

  if (should_update_filename(state))
    state = update_filename(state);

  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);

  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;

  return check_boundaries(state, word_wrap);
  }

app_state enter(app_state state)
  {
  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  if (w.is_command_window) // no enter in command windows
    return state;

  auto& f = state.file_state.files[state.file_state.active_file];
  auto& d = f.dot;

  if (w.piped) // possibly sending a command to the piped child process
    {
    if (on_last_line(f, d.r.p1) && on_last_line(f, d.r.p2))
      {
      d.r.p1 = d.r.p2 = f.content.size();
      std::string piped_cmd = get_piped_command(w.piped_prompt, f);
      std::string text;
      try
        {
        uint32_t idx = get_history_index(w.piped_prompt_history, piped_cmd);
        if (idx == (uint32_t)-1)
          {
          w.piped_prompt_index = (uint32_t)w.piped_prompt_history.size();
          w.piped_prompt_history.push_back(piped_cmd);
          }
        else
          {
          w.piped_prompt_index = idx;
          if (w.piped_prompt_index >= w.piped_prompt_history.size())
            w.piped_prompt_index = 0;
          }
        piped_cmd.push_back('\n');
        JAM::send_to_pipe(w.process, piped_cmd.c_str());
        text = JAM::read_from_pipe(w.process, 100);
        }
      catch (std::runtime_error e)
        {
        w.piped = false;
        w.process = nullptr;
        state = add_error_text(state, e.what());
        return state;
        }

      jamlib::snapshot ss;
      ss.content = f.content;
      ss.dot = f.dot;
      ss.modification_mask = f.modification_mask;
      ss.enc = f.enc;

      jamlib::buffer txt;
      auto tr = txt.transient();
      auto wtext = convert_string_to_wstring(text, f.enc);
      wtext.erase(std::remove(wtext.begin(), wtext.end(), '\r'), wtext.end());
      tr.push_back('\n');
      for (auto ch : wtext)
        {
        tr.push_back(ch);
        }
      txt = tr.persistent();
      f.content = f.content.insert((uint32_t)f.dot.r.p2, txt);
      f.dot.r.p1 = f.content.size();
      f.dot.r.p2 = f.content.size();
      f.modification_mask |= 1;
      f.history = f.history.push_back(ss);
      f.undo_redo_index = f.history.size();
      w.file_pos = get_line_begin(f, w.file_pos);
      w.piped_prompt = get_piped_prompt(f);
      if (should_update_corresponding_command_window(state))
        state = update_corresponding_command_window(state);
      return check_boundaries(state, w.word_wrap);
      }
    }

  std::string command;
  if (d.r.p1 == d.r.p2)
    command.push_back('i');
  else
    {
    command.push_back('c');
    }
  command.push_back('/');

  int64_t p0 = get_begin_of_line(state.file_state.files[state.file_state.active_file]);
  int64_t p1 = state.file_state.files[state.file_state.active_file].dot.r.p1;

  auto it = state.file_state.files[state.file_state.active_file].content.begin() + p0;
  command.push_back('\\');
  command.push_back('n');
  for (; p0 < p1; ++p0, ++it)
    {
    if (*it == ' ')
      command.push_back(' ');
    else if (*it == '\t')
      command.push_back('\t');
    else
      break;
    }
  command.push_back('/');

  state.file_state = *jamlib::handle_command(state.file_state, command);
  state.file_state.files[state.file_state.active_file].dot.r.p1 += command.size() - 4;
  state.file_state.files[state.file_state.active_file].dot.r.p2 = state.file_state.files[state.file_state.active_file].dot.r.p1;

  w.file_pos = get_line_begin(state.file_state.files[state.file_state.active_file], w.file_pos);

  if (should_update_filename(state))
    state = update_filename(state);

  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);

  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;

  return check_boundaries(state, word_wrap);
  }

app_state text_input(app_state state, const char* txt)
  {
  if (invalid_command_window_position(state))
    return state;
  std::string command;
  auto d = state.file_state.files[state.file_state.active_file].dot;
  if (d.r.p1 == d.r.p2)
    command.push_back('i');
  else
    {
    command.push_back('c');
    }
  command.push_back('/');
  const char* ptr = txt;
  while (*ptr)
    {
    if (*ptr == '/' || *ptr == '\\')
      command.push_back('\\');
    command.push_back(*ptr);
    ++ptr;
    }
  command.push_back('/');

  state.file_state = *jamlib::handle_command(state.file_state, command);

  // compute length of text in characters
  auto& f = state.file_state.files[state.file_state.active_file];
  int64_t p1 = f.dot.r.p1;
  int64_t p2 = f.dot.r.p2;
  //auto it = f.content.begin() + p1;
  //auto it_end = f.content.end() + p2;


  f.dot.r.p1 += p2 - p1;
  f.dot.r.p2 = state.file_state.files[state.file_state.active_file].dot.r.p1;

  auto& w = state.windows[state.file_id_to_window_id[state.file_state.active_file]];
  w.file_pos = get_line_begin(state.file_state.files[state.file_state.active_file], w.file_pos);

  if (should_update_filename(state))
    state = update_filename(state);

  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);

  bool word_wrap = state.windows[state.file_id_to_window_id[state.file_state.active_file]].word_wrap;

  return check_boundaries(state, word_wrap);
  }

app_state stop_selection(app_state state)
  {
  if (keyb_data.selecting)
    {
    keyb_data.selecting = false;
    if (keyb_data.selection_id == state.file_state.active_file)
      {
      auto& f = state.file_state.files[state.file_state.active_file];
      f.dot.r.p1 = keyb_data.selection_start;
      f.dot.r.p2 = keyb_data.selection_end;
      if (f.dot.r.p2 < f.dot.r.p1)
        {
        keyb_data.last_selection_was_upward = true;
        std::swap(f.dot.r.p1, f.dot.r.p2);
        }
      else
        keyb_data.last_selection_was_upward = false;
      return state;
      }
    }
  return state;
  }

app_state paste_from_windows_clipboard(app_state state)
  {
  std::string str = get_text_from_windows_clipboard();
  auto wtext = JAM::convert_string_to_wstring(str);
  wtext.erase(std::remove(wtext.begin(), wtext.end(), '\r'), wtext.end());
  str = JAM::convert_wstring_to_string(wtext);
  return text_input(state, str.c_str());
  }

app_state paste_from_snarf_buffer(app_state state)
  {
  auto& f = state.file_state.files[state.file_state.active_file];

  jamlib::snapshot ss;
  ss.content = f.content;
  ss.dot = f.dot;
  ss.modification_mask = f.modification_mask;
  ss.enc = f.enc;

  if (f.dot.r.p1 != f.dot.r.p2)
    f.content = f.content.erase((uint32_t)f.dot.r.p1, (uint32_t)f.dot.r.p2);
  f.content = f.content.insert((uint32_t)f.dot.r.p1, state.snarf_buffer);
  f.dot.r.p2 = f.dot.r.p1 + state.snarf_buffer.size();
  f.modification_mask |= 1;

  f.history = f.history.push_back(ss);
  f.undo_redo_index = f.history.size();

  if (should_update_filename(state))
    state = update_filename(state);

  if (should_update_corresponding_command_window(state))
    state = update_corresponding_command_window(state);

  return update_window_file_pos(state);
  }

app_state copy_to_snarf_buffer(app_state state)
  {
  const auto& f = state.file_state.files[state.file_state.active_file];
  state.snarf_buffer = f.content.slice(f.dot.r.p1, f.dot.r.p2);
  return state;
  }

void copy_to_windows_clipboard(const app_state& state)
  {
  const auto& f = state.file_state.files[state.file_state.active_file];
  auto it = f.content.begin() + f.dot.r.p1;
  auto it_end = f.content.begin() + f.dot.r.p2;
  /*
  str.reserve(std::distance(it, it_end));
  switch (f.enc)
    {
    case jamlib::ENC_UTF8:
    {
    utf8::utf16to8(it, it_end, std::back_inserter(str));
    break;
    }
    case jamlib::ENC_ASCII:
    {
    for (; it != it_end; ++it)
      str.push_back((char)*it);
    }
    }
  */
  std::string str = get_utf8_string(it, it_end, f.enc);
  copy_to_windows_clipboard(str);
  }

app_state check_pipes(bool& modifications, app_state state)
  {
  modifications = false;
  size_t sz = state.windows.size();
  for (size_t i = 0; i < sz; ++i)
    {
    auto& w = state.windows[i];
    if (w.piped)
      {
      std::string text;
      auto& f = state.file_state.files[w.file_id];
      try
        {
        text = JAM::read_from_pipe(w.process, 10);
        }
      catch (std::runtime_error e)
        {
        w.piped = false;
        w.process = nullptr;
        state = add_error_text(state, e.what());
        return state;
        }
      if (text.empty())
        continue;
      modifications = true;
      jamlib::snapshot ss;
      ss.content = f.content;
      ss.dot = f.dot;
      ss.modification_mask = f.modification_mask;
      ss.enc = f.enc;

      jamlib::buffer txt;
      auto tr = txt.transient();
      auto wtext = jamlib::convert_string_to_wstring(text, f.enc);
      wtext.erase(std::remove(wtext.begin(), wtext.end(), '\r'), wtext.end());
      tr.push_back('\n');
      for (auto ch : wtext)
        {
        tr.push_back(ch);
        }
      txt = tr.persistent();
      f.content = f.content.insert((uint32_t)f.dot.r.p1, txt);
      f.dot.r.p1 = f.content.size();
      f.dot.r.p2 = f.content.size();
      f.modification_mask |= 1;
      f.history = f.history.push_back(ss);
      f.undo_redo_index = f.history.size();
      w.file_pos = get_line_begin(f, w.file_pos);
      w.piped_prompt = get_piped_prompt(f);
      if (should_update_corresponding_command_window(state))
        state = update_corresponding_command_window(state);
      state = check_boundaries(state, w.word_wrap);
      }
    }
  return state;
  }

bool valid_char_for_word_selection(wchar_t ch)
  {
  bool valid = false;
  valid |= (ch >= 48 && ch <= 57); // [0 : 9]
  valid |= (ch >= 97 && ch <= 122); // [a : z]
  valid |= (ch >= 65 && ch <= 90); // [A : Z]
  valid |= (ch == 95); // _  c++: naming
  valid |= (ch == 33); // !  scheme: vector-set!
  valid |= (ch == 63); // ?  scheme: eq?
  valid |= (ch == 45); // -  scheme: list-ref
  valid |= (ch == 42); // *  scheme: let*
  return valid;
  }

app_state select_word(app_state state, int64_t id, int64_t pos)
  {
  if (pos < 0 || id < 0 || state.file_state.files[id].content.empty())
    return state;
  const auto it0 = state.file_state.files[id].content.begin();
  auto it = state.file_state.files[id].content.begin() + pos;
  auto it2 = it;
  auto it_end = state.file_state.files[id].content.end();
  if (it == it_end)
    --it;
  while (it > it0)
    {
    if (!valid_char_for_word_selection(*it))
      break;
    --it;
    }
  if (!valid_char_for_word_selection(*it))
    ++it;
  while (it2 < it_end)
    {
    if (!valid_char_for_word_selection(*it2))
      break;
    ++it2;
    }
  if (it2 <= it)
    return state;
  int64_t p1 = (int64_t)std::distance(it0, it);
  int64_t p2 = (int64_t)std::distance(it0, it2);

  // now check special cases
  // first special: case var-> : will select var- because of scheme rule, but here we're c++ and we don't want the -

  if (p2 < state.file_state.files[id].content.size())
    {
    if (state.file_state.files[id].content[p2] == '>' && state.file_state.files[id].content[p2 - 1] == '-')
      --p2;
    }

  state.file_state.files[id].dot.r.p1 = p1;
  state.file_state.files[id].dot.r.p2 = p2;
  return state;
  }

screen_ex_pixel skip_last_line_feed_character(const app_state& state, screen_ex_pixel in, int64_t current_p1, int64_t current_p2)
  {
  if (in.id < 0)
    return in;
  if (current_p1 > current_p2)
    return in;
  if (current_p1 >= in.pos)
    return in;
  if (in.pos && in.pos < state.file_state.files[in.id].content.size())
    {
    auto it = state.file_state.files[in.id].content.begin() + in.pos;
    if (*it == '\n' && *(--it) != '\n')
      {
      --in.pos;
      }
    }
  return in;
  }

screen_ex_pixel find_closest_id(int x, int y, const app_state& state)
  {
  auto p = get_ex(y, x);
  if (p.id != -1)
    {
    return p;
    }
  else // find best guess
    {
    int save_x = x;
    int save_y = y;
    while (x > 0)
      {
      p = get_ex(y, --x);
      if (p.id != -1)
        return p;
      }
    x = save_x;
    y = save_y;
    while (x < state.w)
      {
      p = get_ex(y, ++x);
      if (p.id != -1)
        return p;
      }
    x = save_x;
    y = save_y;
    while (y > 0)
      {
      p = get_ex(--y, x);
      if (p.id != -1)
        return p;
      }
    x = save_x;
    y = save_y;
    while (y < state.h)
      {
      p = get_ex(++y, x);
      if (p.id != -1)
        return p;
      }
    }
  return get_ex(y, x);
  }

std::optional<app_state> process_input(app_state state, const settings& sett)
  {
  SDL_Event event;
  auto tic = std::chrono::steady_clock::now();
  for (;;)
    {
    while (SDL_PollEvent(&event))
      {
      keyb.handle_event(event);
      switch (event.type)
        {
        case SDL_DROPFILE:
        {
        auto dropped_filedir = event.drop.file;
        int x, y;
        SDL_GetMouseState(&x, &y);
        x /= font_width;
        y /= font_height;
        auto p = find_closest_id(x, y, state);
        std::string path(dropped_filedir);
        SDL_free(dropped_filedir);    // Free dropped_filedir memory
        return load_file(path, state, p.id);
        break;
        }
        case SDL_WINDOWEVENT:
        {
        if (event.window.event == SDL_WINDOWEVENT_RESIZED)
          {
          auto new_w = event.window.data1;
          auto new_h = event.window.data2;
          state.w = new_w;
          state.h = new_h;

          resize_term(new_h / font_height, new_w / font_width);
          resize_term_ex(new_h / font_height, new_w / font_width);

          SDL_Rect dest;

          dest.x = 0;
          dest.y = 0;
          dest.w = state.w;
          dest.h = state.h;

          SDL_FillRect(pdc_screen, &dest, SDL_MapRGB(pdc_screen->format, (uint8_t)sett.win_bg_red, (uint8_t)sett.win_bg_green, (uint8_t)sett.win_bg_blue));

          return resize(state);
          }
        break;
        }
        case SDL_QUIT: return exit_command(state, -1, "");
        case SDL_TEXTINPUT:
          if (state.file_state.files[state.file_state.active_file].dot.r.p2 < state.file_state.files[state.file_state.active_file].dot.r.p1)
            std::swap(state.file_state.files[state.file_state.active_file].dot.r.p1, state.file_state.files[state.file_state.active_file].dot.r.p2);
          if (keyb_data.selecting)
            state = stop_selection(state);
          return text_input(state, event.text.text);
          break;

        case SDL_KEYDOWN:
        {
        if (state.file_state.files[state.file_state.active_file].dot.r.p2 < state.file_state.files[state.file_state.active_file].dot.r.p1)
          std::swap(state.file_state.files[state.file_state.active_file].dot.r.p1, state.file_state.files[state.file_state.active_file].dot.r.p2);
        switch (event.key.keysym.sym)
          {
          //case SDLK_ESCAPE: return std::nullopt;
          case SDLK_HOME: return move_to_begin_of_line(state);
          case SDLK_END: return move_to_end_of_line(state);
          case SDLK_LEFT: return move_left(state);
          case SDLK_RIGHT: return move_right(state);
          case SDLK_DOWN: return move_down(state);
          case SDLK_UP: return move_up(state);
          case SDLK_PAGEUP: return move_cursor_page_up(state);
          case SDLK_PAGEDOWN: return move_cursor_page_down(state);
          case SDLK_BACKSPACE: if (keyb_data.selecting) state = stop_selection(state); return backspace(state);
          case SDLK_RETURN: if (keyb_data.selecting) state = stop_selection(state); return enter(state);
          case SDLK_TAB: if (keyb_data.selecting) state = stop_selection(state); return tab(state);
          case SDLK_DELETE: if (keyb_data.selecting) state = stop_selection(state); return delete_press(state, (keyb.is_down(SDLK_LSHIFT) || keyb.is_down(SDLK_RSHIFT)));
          case SDLK_LSHIFT:
          case SDLK_RSHIFT:
          {
          if (keyb_data.selecting)
            break;
          keyb_data.selecting = true;
          keyb_data.selection_id = state.file_state.active_file;
          keyb_data.selection_start = state.file_state.files[keyb_data.selection_id].dot.r.p1;
          keyb_data.selection_end = state.file_state.files[keyb_data.selection_id].dot.r.p2;
          if (keyb_data.selection_start != keyb_data.selection_end)
            {
            if (keyb_data.last_selection_was_upward)
              {
              std::swap(keyb_data.selection_start, keyb_data.selection_end);
              state.file_state.files[keyb_data.selection_id].dot.r.p2 = state.file_state.files[keyb_data.selection_id].dot.r.p1;
              }
            else
              state.file_state.files[keyb_data.selection_id].dot.r.p1 = state.file_state.files[keyb_data.selection_id].dot.r.p2;
            return state;
            }
          break;
          }
          case SDLK_a:
          {
          if (keyb.is_down(SDLK_LCTRL) || keyb.is_down(SDLK_RCTRL)) // select all
            {
            return select_all_command(state);
            }
          break;
          }
          case SDLK_c:
          {
          if (keyb.is_down(SDLK_LCTRL) || keyb.is_down(SDLK_RCTRL)) // copy
            {
            if (keyb.is_down(SDLK_LSHIFT) || keyb.is_down(SDLK_RSHIFT)) // copy to clipboard
              {
              if (keyb_data.selecting)
                state = stop_selection(state);
              copy_to_windows_clipboard(state);
              return state;
              }
            else
              return copy_to_snarf_buffer(state);
            }
          break;
          }
          case SDLK_v:
          {
          if (keyb.is_down(SDLK_LCTRL) || keyb.is_down(SDLK_RCTRL)) // paste
            {
            if (keyb.is_down(SDLK_LSHIFT) || keyb.is_down(SDLK_RSHIFT)) // paste from clipboard        
              {
              if (keyb_data.selecting)
                state = stop_selection(state);
              return paste_from_windows_clipboard(state);
              }
            return paste_from_snarf_buffer(state);
            }
          break;
          }
          case SDLK_y:
          {
          if (keyb.is_down(SDLK_LCTRL) || keyb.is_down(SDLK_RCTRL)) // redo
            {
            return local_redo_command(state, state.file_state.active_file, "");
            }
          break;
          }
          case SDLK_z:
          {
          if (keyb.is_down(SDLK_LCTRL) || keyb.is_down(SDLK_RCTRL)) // undo
            {
            return local_undo_command(state, state.file_state.active_file, "");
            }
          break;
          }
          case SDLK_s:
          {
          if (keyb.is_down(SDLK_LCTRL) || keyb.is_down(SDLK_RCTRL)) // save
            {
            return put_command(state, state.file_state.active_file, "");
            }
          break;
          }
          default: break;
          }
        break;
        }
        case SDL_KEYUP:
        {
        if (state.file_state.files[state.file_state.active_file].dot.r.p2 < state.file_state.files[state.file_state.active_file].dot.r.p1)
          std::swap(state.file_state.files[state.file_state.active_file].dot.r.p1, state.file_state.files[state.file_state.active_file].dot.r.p2);
        switch (event.key.keysym.sym)
          {
          case SDLK_LSHIFT:
          {
          if (keyb_data.selecting)
            return stop_selection(state);
          break;
          }
          case SDLK_RSHIFT:
          {
          if (keyb_data.selecting)
            return stop_selection(state);
          break;
          }
          default: break;
          }
        break;
        }
        case SDL_MOUSEMOTION:
        {
        mouse.prev_mouse_x = mouse.mouse_x;
        mouse.prev_mouse_y = mouse.mouse_y;
        mouse.mouse_x = event.motion.x;
        mouse.mouse_y = event.motion.y;
        if (mouse.left_button_down)
          mouse.left_dragging = true;
        if (mouse.middle_button_down)
          {
          mouse.middle_dragging = true;
          int x = event.motion.x / font_width;
          int y = event.motion.y / font_height;
          auto p = get_ex(y, x);
          while (x > 0 && p.id < 0)
            {
            p = get_ex(y, --x);
            }
          if (mouse.middle_drag_start.id == -1 || mouse.middle_drag_start.type != SET_TEXT)
            mouse.middle_drag_start = p;
          if (p.id == mouse.middle_drag_start.id && p.type == SET_TEXT)
            {
            mouse.middle_drag_end = skip_last_line_feed_character(state, p, mouse.middle_drag_start.pos, mouse.middle_drag_end.pos);
            return state;
            }
          if (mouse.middle_drag_start.id == -1 || mouse.middle_drag_start.type != SET_TEXT)
            return state;
          // we're not inside or window. try to find the next best guess.
          x = event.motion.x / font_width;
          y = event.motion.y / font_height;
          p = get_ex(y, x);
          bool jump_to_begin_or_end = true;
          int save_x = x;
          int save_y = y;
          if (p.id != mouse.middle_drag_start.id || p.type != SET_TEXT)
            {
            while (x < (state.w / font_width))
              {
              p = get_ex(y, ++x);
              if (p.id == mouse.middle_drag_start.id  && p.type == SET_TEXT)
                break;
              }
            }
          if (p.id != mouse.middle_drag_start.id || p.type != SET_TEXT)
            {
            x = save_x;
            y = save_y;
            while (x > 0)
              {
              p = get_ex(y, --x);
              if (p.id == mouse.middle_drag_start.id && p.type == SET_TEXT)
                break;
              }
            }
          // for finding good positions up/down for selection, we move the position to the most left cursor position in the window
          if (!state.windows[state.file_id_to_window_id[mouse.middle_drag_start.id]].is_command_window)
            save_x = state.windows[state.file_id_to_window_id[mouse.middle_drag_start.id]].outer_x + OFFSET_FROM_SCROLLBAR;
          if (p.id != mouse.middle_drag_start.id || p.type != SET_TEXT)
            {
            jump_to_begin_or_end = false;
            x = save_x;
            y = save_y;
            while (y > 0)
              {
              p = get_ex(--y, x);
              if (p.id == mouse.middle_drag_start.id && p.type == SET_TEXT)
                break;
              }
            }
          if (p.id != mouse.middle_drag_start.id || p.type != SET_TEXT)
            {
            jump_to_begin_or_end = false;
            x = save_x;
            y = save_y;
            while (y < (state.h / font_height))
              {
              p = get_ex(++y, x);
              if (p.id == mouse.middle_drag_start.id && p.type == SET_TEXT)
                break;
              }
            }
          if (p.id == mouse.middle_drag_start.id && p.type == SET_TEXT)
            {
            if (jump_to_begin_or_end)
              {
              if (mouse.middle_drag_start.pos < p.pos)
                p.pos = get_line_end(state.file_state.files[p.id], p.pos);
              else
                p.pos = get_line_begin(state.file_state.files[p.id], p.pos);
              }
            mouse.middle_drag_end = skip_last_line_feed_character(state, p, mouse.middle_drag_start.pos, mouse.middle_drag_end.pos);
            }
          return state;
          }
        if (mouse.right_button_down)
          {
          mouse.right_dragging = true;
          int x = event.motion.x / font_width;
          int y = event.motion.y / font_height;
          auto p = get_ex(y, x);
          while (x > 0 && p.id < 0)
            {
            p = get_ex(y, --x);
            }
          if (mouse.right_drag_start.id == -1 || mouse.right_drag_start.type != SET_TEXT)
            mouse.right_drag_start = p;
          if (p.id == mouse.right_drag_start.id && p.type == SET_TEXT)
            {
            mouse.right_drag_end = skip_last_line_feed_character(state, p, mouse.right_drag_start.pos, mouse.right_drag_end.pos);
            return state;
            }
          if (mouse.right_drag_start.id == -1 || mouse.right_drag_start.type != SET_TEXT)
            return state;
          // we're not inside or window. try to find the next best guess.
          x = event.motion.x / font_width;
          y = event.motion.y / font_height;
          p = get_ex(y, x);
          bool jump_to_begin_or_end = true;
          int save_x = x;
          int save_y = y;
          if (p.id != mouse.right_drag_start.id || p.type != SET_TEXT)
            {
            while (x < (state.w / font_width))
              {
              p = get_ex(y, ++x);
              if (p.id == mouse.right_drag_start.id  && p.type == SET_TEXT)
                break;
              }
            }
          if (p.id != mouse.right_drag_start.id || p.type != SET_TEXT)
            {
            x = save_x;
            y = save_y;
            while (x > 0)
              {
              p = get_ex(y, --x);
              if (p.id == mouse.right_drag_start.id && p.type == SET_TEXT)
                break;
              }
            }
          // for finding good positions up/down for selection, we move the position to the most left cursor position in the window
          if (!state.windows[state.file_id_to_window_id[mouse.right_drag_start.id]].is_command_window)
            save_x = state.windows[state.file_id_to_window_id[mouse.right_drag_start.id]].outer_x + OFFSET_FROM_SCROLLBAR;
          if (p.id != mouse.right_drag_start.id || p.type != SET_TEXT)
            {
            jump_to_begin_or_end = false;
            x = save_x;
            y = save_y;
            while (y > 0)
              {
              p = get_ex(--y, x);
              if (p.id == mouse.right_drag_start.id && p.type == SET_TEXT)
                break;
              }
            }
          if (p.id != mouse.right_drag_start.id || p.type != SET_TEXT)
            {
            jump_to_begin_or_end = false;
            x = save_x;
            y = save_y;
            while (y < (state.h / font_height))
              {
              p = get_ex(++y, x);
              if (p.id == mouse.right_drag_start.id && p.type == SET_TEXT)
                break;
              }
            }
          if (p.id == mouse.right_drag_start.id && p.type == SET_TEXT)
            {
            if (jump_to_begin_or_end)
              {
              if (mouse.right_drag_start.pos < p.pos)
                p.pos = get_line_end(state.file_state.files[p.id], p.pos);
              else
                p.pos = get_line_begin(state.file_state.files[p.id], p.pos);
              }
            mouse.right_drag_end = skip_last_line_feed_character(state, p, mouse.right_drag_start.pos, mouse.right_drag_end.pos);
            }
          return state;
          }
        if (mouse.left_dragging)
          {
          if (mouse.rearranging_windows)
            {
            move(mouse.rwd.y, mouse.rwd.x - 1);
            if (mouse.rwd.x - 1 > 0)
              addch(mouse.rwd.current_sign_left);
            move(mouse.rwd.y, mouse.rwd.x);
            addch(mouse.rwd.current_sign_mid);
            move(mouse.rwd.y, mouse.rwd.x + 1);
            if (mouse.rwd.x + 1 < SP->cols)
              addch(mouse.rwd.current_sign_right);
            int x = event.motion.x / font_width;
            int y = event.motion.y / font_height;
            mouse.rwd.x = x;
            mouse.rwd.y = y;
            mouse.rwd.current_sign_left = mvinch(y, x - 1);
            mouse.rwd.current_sign_mid = mvinch(y, x);
            mouse.rwd.current_sign_right = mvinch(y, x + 1);
            move(y, x - 1);
            if (mouse.rwd.x - 1 > 0)
              addch(mouse.rwd.icon_sign);
            move(y, x);
            addch(mouse.rwd.icon_sign);
            move(y, x + 1);
            if (mouse.rwd.x + 1 < SP->cols)
              addch(mouse.rwd.icon_sign);
            refresh();
            SDL_UpdateWindowSurface(pdc_window);
            break;
            }
          else
            {
            int x = event.motion.x / font_width;
            int y = event.motion.y / font_height;
            auto p = get_ex(y, x);
            while (x > 0 && p.id < 0)
              {
              p = get_ex(y, --x);
              }
            if (mouse.left_drag_start.id == -1 || mouse.left_drag_start.type != SET_TEXT)
              mouse.left_drag_start = p;
            //if (p.id == (int64_t)state.file_state.active_file && p.type == SET_TEXT)
            if (p.id == mouse.left_drag_start.id && p.type == SET_TEXT)
              {
              state.file_state.active_file = p.id;
              //state.file_state.files[p.id].dot.r.p2 = p.pos;
              if (p.id == mouse.left_drag_start.id)
                {
                mouse.left_drag_end = skip_last_line_feed_character(state, p, mouse.left_drag_start.pos, mouse.left_drag_end.pos);
                int64_t p1 = mouse.left_drag_start.pos;
                int64_t p2 = mouse.left_drag_end.pos;
                if (p1 > p2)
                  {
                  --p1; // toggles pivot point of selection
                  if (!keyb_data.selecting)
                    keyb_data.last_selection_was_upward = true;
                  std::swap(p1, p2);
                  }
                else if (!keyb_data.selecting)
                  keyb_data.last_selection_was_upward = false;
                state.file_state.files[p.id].dot.r.p1 = p1;
                state.file_state.files[p.id].dot.r.p2 = p2 < state.file_state.files[p.id].content.size() ? p2 + 1 : state.file_state.files[p.id].content.size();
                }
              return state;
              }
            if (mouse.left_drag_start.id == -1 || mouse.left_drag_start.type != SET_TEXT)
              {
              mouse.left_dragging = false;
              return state;
              }
            // we're not inside or window. try to find the next best guess.
            x = event.motion.x / font_width;
            y = event.motion.y / font_height;
            p = get_ex(y, x);
            bool jump_to_begin_or_end = false;
            int save_x = x;
            int save_y = y;
            if (p.id != mouse.left_drag_start.id || p.type != SET_TEXT)
              {
              while (x < (state.w / font_width))
                {
                p = get_ex(y, ++x);
                if (p.id == mouse.left_drag_start.id  && p.type == SET_TEXT)
                  break;
                }
              }
            if (p.id != mouse.left_drag_start.id || p.type != SET_TEXT)
              {
              x = save_x;
              y = save_y;
              while (x > 0)
                {
                p = get_ex(y, --x);
                if (p.id == mouse.left_drag_start.id && p.type == SET_TEXT)
                  break;
                }
              }
            // for finding good positions up/down for selection, we move the position to the most left cursor position in the window
            if (!state.windows[state.file_id_to_window_id[mouse.left_drag_start.id]].is_command_window)
              save_x = state.windows[state.file_id_to_window_id[mouse.left_drag_start.id]].outer_x + OFFSET_FROM_SCROLLBAR;
            if (p.id != mouse.left_drag_start.id || p.type != SET_TEXT)
              {
              jump_to_begin_or_end = false;
              x = save_x;
              y = save_y;
              while (y > 0)
                {
                p = get_ex(--y, x);
                if (p.id == mouse.left_drag_start.id && p.type == SET_TEXT)
                  break;
                }
              }
            if (p.id != mouse.left_drag_start.id || p.type != SET_TEXT)
              {
              jump_to_begin_or_end = false;
              x = save_x;
              y = save_y;
              while (y < (state.h / font_height))
                {
                p = get_ex(++y, x);
                if (p.id == mouse.left_drag_start.id && p.type == SET_TEXT)
                  break;
                }
              }
            if (p.id == mouse.left_drag_start.id && p.type == SET_TEXT)
              {
              if (jump_to_begin_or_end)
                {
                if (mouse.left_drag_start.pos < p.pos)
                  p.pos = get_line_end(state.file_state.files[p.id], p.pos);
                else
                  p.pos = get_line_begin(state.file_state.files[p.id], p.pos);
                }
              mouse.left_drag_end = skip_last_line_feed_character(state, p, mouse.left_drag_start.pos, mouse.left_drag_end.pos);
              int64_t p1 = mouse.left_drag_start.pos;
              int64_t p2 = mouse.left_drag_end.pos;
              if (p1 > p2)
                {
                --p1;  // toggles pivot point of selection
                if (!keyb_data.selecting)
                  keyb_data.last_selection_was_upward = true;
                std::swap(p1, p2);
                }
              else if (!keyb_data.selecting)
                keyb_data.last_selection_was_upward = false;
              state.file_state.files[p.id].dot.r.p1 = p1;
              state.file_state.files[p.id].dot.r.p2 = p2 < state.file_state.files[p.id].content.size() ? p2 + 1 : state.file_state.files[p.id].content.size();
              return state;
              }
            }
          }
        /*
      int x = event.motion.x / font_width;
      int y = event.motion.y / font_height;
      auto p = get_ex(y, x);
      if (p.id != -1)
        {
        if (!state.windows[state.file_id_to_window_id[p.id]].is_command_window)
          state.file_state.active_file = p.id;
        return state;
        }
      else // find best guess
        {
        int save_x = x;
        int save_y = y;
        while (x > 0)
          {
          p = get_ex(y, --x);
          if (p.id != -1)
            {
            if (!state.windows[state.file_id_to_window_id[p.id]].is_command_window)
              state.file_state.active_file = p.id;
            return state;
            }
          }
        x = save_x;
        y = save_y;
        while (x < state.w)
          {
          p = get_ex(y, ++x);
          if (p.id != -1)
            {
            if (!state.windows[state.file_id_to_window_id[p.id]].is_command_window)
              state.file_state.active_file = p.id;
            return state;
            }
          }
        x = save_x;
        y = save_y;
        while (y > 0)
          {
          p = get_ex(--y, x);
          if (p.id != -1)
            {
            if (!state.windows[state.file_id_to_window_id[p.id]].is_command_window)
              state.file_state.active_file = p.id;
            return state;
            }
          }
        x = save_x;
        y = save_y;
        while (y < state.h)
          {
          p = get_ex(++y, x);
          if (p.id != -1)
            {
            if (!state.windows[state.file_id_to_window_id[p.id]].is_command_window)
              state.file_state.active_file = p.id;
            return state;
            }
          }
        }*/
        break;
        }
        case SDL_MOUSEBUTTONDOWN:
        {
        mouse.mouse_x_at_button_press = event.button.x;
        mouse.mouse_y_at_button_press = event.button.y;
        if (event.button.button == 1)
          {
          if (event.button.clicks == 2) // double click
            {
            int x = event.button.x / font_width;
            int y = event.button.y / font_height;
            auto p = get_ex(y, x);
            if (p.id >= 0 && p.type == SET_TEXT)
              {
              return select_word(state, p.id, p.pos);
              }            
            }
          mouse.left_button_down = true;
          int x = event.button.x / font_width;
          int y = event.button.y / font_height;
          auto p = get_ex(y, x);
          if (p.id >= 0 && p.type == SET_ICON)
            {
            mouse.rearranging_windows = true;
            mouse.rwd.rearranging_file_id = p.id;
            mouse.rwd.x = x;
            mouse.rwd.y = y;
            mouse.rwd.current_sign_left = mvinch(y, x - 1);
            mouse.rwd.current_sign_mid = mvinch(y, x);
            mouse.rwd.current_sign_right = mvinch(y, x + 1);
            mouse.rwd.icon_sign = mouse.rwd.current_sign_mid;
            return state;
            }
          while (x > 0 && p.id < 0)
            {
            p = get_ex(y, --x);
            }
          //mouse.left_drag_start = skip_last_line_feed_character(state, p);
          mouse.left_drag_start = p;
          if (p.id >= 0)
            {
            if (p.type == SET_TEXT)
              {
              if (state.file_state.active_file == p.id)
                {
                state.file_state.files[p.id].dot.r.p1 = p.pos;
                state.file_state.files[p.id].dot.r.p2 = p.pos;
                }
              else
                {// if the active file is different than the window where we click, don't change the dot yet, unless dot is empty or the window is a command window
                if (state.file_state.files[p.id].dot.r.p1 == state.file_state.files[p.id].dot.r.p2 || state.windows[state.file_id_to_window_id[p.id]].is_command_window)
                  {
                  state.file_state.files[p.id].dot.r.p1 = p.pos;
                  state.file_state.files[p.id].dot.r.p2 = p.pos;
                  }
                }
              }
            //state.file_state.active_file = p.id;
            return state;
            }
          }
        else if (event.button.button == 2)
          {
          int x = event.button.x / font_width;
          int y = event.button.y / font_height;
          auto p = get_ex(y, x);
          //mouse.middle_drag_start = skip_last_line_feed_character(state, p);
          mouse.middle_drag_start = p;
          mouse.middle_button_down = true;
          }
        else if (event.button.button == 3)
          {
          int x = event.button.x / font_width;
          int y = event.button.y / font_height;
          auto p = get_ex(y, x);
          //mouse.right_drag_start = skip_last_line_feed_character(state, p);
          mouse.right_drag_start = p;
          mouse.right_button_down = true;
          }
        break;
        }
        case SDL_MOUSEBUTTONUP:
        {
        if (event.button.button == 1)
          {
          int x = event.button.x / font_width;
          int y = event.button.y / font_height;
          auto p = get_ex(y, x);

          if (mouse.rearranging_windows)
            {
            mouse.left_button_down = false;
            mouse.left_dragging = false;
            mouse.rearranging_windows = false;
            if (p.id == mouse.rwd.rearranging_file_id && p.type == SET_ICON)
              {
              return enlarge_window(state, p.id);
              }
            return adapt_grid(state, event.button.x / font_width, event.button.y / font_height);
            }
          bool was_dragging = mouse.left_dragging;
          mouse.left_button_down = false;
          mouse.left_dragging = false;
          mouse.rearranging_windows = false;

          if (was_dragging)
            {
            state.file_state.active_file = mouse.left_drag_start.id;
            return state;
            }

          if (p.id >= 0 && p.type == SET_SCROLLBAR)
            {
            state.file_state.active_file = p.id;
            double scroll_fract = get_scroll_fraction(x, y, p.id, state);
            state.windows[state.file_id_to_window_id[p.id]].scroll_fraction = scroll_fract;
            const auto& w = state.windows[state.file_id_to_window_id[p.id]];
            int64_t steps = (int64_t)(scroll_fract*(w.rows - 1));
            if (steps <= 0)
              steps = 1;
            return move_page_up_without_cursor(state, steps);
            }

          while (x > 0 && p.id < 0)
            {
            p = get_ex(y, --x);
            }

          if (p.id >= 0)
            {
            if (p.type != SET_TEXT && state.file_state.active_file == p.id)// we're clicking at the end of the file where no data is available. Put cursor at the end.
              { // if the active file is different than the window where we click, don't change the dot yet.
              state.file_state.files[p.id].dot.r.p1 = state.file_state.files[p.id].content.size();
              state.file_state.files[p.id].dot.r.p2 = state.file_state.files[p.id].content.size();
              }
            state.file_state.active_file = p.id;
            if (keyb_data.selecting && p.id == keyb_data.selection_id)
              {
              keyb_data.selection_end = p.pos;
              }

            return state;
            }

          }
        else if (event.button.button == 2)
          {
          bool was_dragging = mouse.middle_dragging; // if dragging, then use selection, otherwise extend selection to clicked-on word
          mouse.middle_button_down = false;
          mouse.middle_dragging = false;
          if (mouse.left_button_down) // chord 1-2 = Cut
            {
            return cut_command(state, mouse.left_drag_start.id, "");
            }
          int x = event.button.x / font_width;
          int y = event.button.y / font_height;
          auto p = get_ex(y, x);
          if (p.type == SET_SCROLLBAR)
            {
            return jump_to_pos(p.pos, (uint32_t)p.id, state);
            }
          if (p.type == SET_ICON)
            {
            return enlarge_window_as_much_as_possible(state, p.id);
            }
          if (was_dragging || p.type == SET_TEXT)
            {
            int64_t id = p.id;
            int64_t p1 = p.pos;
            int64_t p2 = p.pos;
            if (was_dragging)
              {
              id = mouse.middle_drag_start.id;
              p1 = mouse.middle_drag_start.pos;
              p2 = mouse.middle_drag_end.pos;
              if (p2 < p1)
                {
                --p1; // toggle pivot point
                std::swap(p2, p1);
                }
              p2 = p2 < state.file_state.files[id].content.size() ? p2 + 1 : state.file_state.files[id].content.size();
              }
            return execute(state, p1, p2, id);
            }
          return state;
          }
        else if (event.button.button == 3)
          {
          bool was_dragging = mouse.right_dragging; // if dragging, then use selection, otherwise extend selection to clicked-on word
          mouse.right_button_down = false;
          mouse.right_dragging = false;
          if (mouse.left_button_down) // chord 1-3 = Paste
            {
            return paste_command(state, mouse.left_drag_start.id, "");
            }
          int x = event.button.x / font_width;
          int y = event.button.y / font_height;
          auto p = get_ex(y, x);

          if (p.type == SET_ICON)
            {
            return maximize_window(state, p.id);
            }

          if (p.id >= 0 && p.type == SET_SCROLLBAR)
            {
            state.file_state.active_file = p.id;
            double scroll_fract = get_scroll_fraction(x, y, p.id, state);
            state.windows[state.file_id_to_window_id[p.id]].scroll_fraction = scroll_fract;
            const auto& w = state.windows[state.file_id_to_window_id[p.id]];
            int64_t steps = (int64_t)(scroll_fract*(w.rows - 1));
            if (steps <= 0)
              steps = 1;
            return move_page_down_without_cursor(state, steps);
            return state;
            }

          if (was_dragging || p.type == SET_TEXT)
            {
            int64_t id = p.id;
            int64_t p1 = p.pos;
            int64_t p2 = p.pos;
            if (was_dragging)
              {
              id = mouse.right_drag_start.id;
              p1 = mouse.right_drag_start.pos;
              p2 = mouse.right_drag_end.pos;
              if (p2 < p1)
                {
                --p1; // toggle pivot point
                std::swap(p2, p1);
                }
              p2 = p2 < state.file_state.files[id].content.size() ? p2 + 1 : state.file_state.files[id].content.size();
              }
            return load(state, p1, p2, id);
            }

          return state;
          }
        break;
        }
        case SDL_MOUSEWHEEL:
        {
        if (keyb.is_down(SDLK_LCTRL) || keyb.is_down(SDLK_RCTRL))
          {
          if (event.wheel.y > 0)
            ++pdc_font_size;
          else if (event.wheel.y < 0)
            --pdc_font_size;
          if (pdc_font_size < 1)
            pdc_font_size = 1;
          return resize_font(state, pdc_font_size);
          }
        auto active_file = state.file_state.active_file;
        auto id = find_window_pair_id(mouse.mouse_x / font_width, mouse.mouse_y / font_height, state);
        if (id >= 0)
          {
          state.file_state.active_file = state.windows[state.window_pairs[id].window_id].file_id;
          //int steps = state.windows[state.window_pairs[id].window_id].rows - 1;
          int64_t steps = (int64_t)(state.windows[state.window_pairs[id].window_id].scroll_fraction*(state.windows[state.window_pairs[id].window_id].rows - 1));
          if (steps <= 0)
            steps = 1;
          if (event.wheel.y > 0)
            state = move_page_up_without_cursor(state, steps);
          if (event.wheel.y < 0)
            state = move_page_down_without_cursor(state, steps);
          state.file_state.active_file = active_file;
          return state;
          }
        break;
        }
        default: break;
        }
      }
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(16.0));

    auto toc = std::chrono::steady_clock::now();
    auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(toc - tic).count();
    if (time_elapsed > 1000)
      {
      bool modifications;
      state = check_pipes(modifications, state);
      tic = std::chrono::steady_clock::now();
      if (modifications)
        return state;
      }

    }
  //return std::nullopt;
  }

void engine::run()
  {
  state = draw(state, sett);
  SDL_UpdateWindowSurface(pdc_window);
  while (auto new_state = process_input(state, sett))
    {
    state = draw(*new_state, sett);

    SDL_UpdateWindowSurface(pdc_window);
    }
  }