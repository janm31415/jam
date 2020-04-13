#include "window.h"
#include "colors.h"
#include "pdcex.h"
#include "utils.h"
#include <string>
#include <sstream>
#include <set>
#include <algorithm>
#include <cctype>

#include <curses.h>
#include <jam_filename.h>

#include "mouse.h"
#include "keyboard.h"
#include "syntax_highlight.h"

#include <jam_encoding.h>
#include <jam_pipe.h>

extern "C"
  {
#include <sdl2/pdcsdl.h>
  }

namespace
  {

  bool has_syntax_highlight(comment_data& cd, const std::string& filename_or_extension)
    {
    static syntax_highlighter sh;
    if (sh.extension_or_filename_has_syntax_highlighter(filename_or_extension))
      {
      cd = sh.get_syntax_highlighter(filename_or_extension);
      return true;
      }
    return false;
    }

#define ALL_CHARS_FORMAT A_BOLD

  bool character_invert(uint32_t character, const settings& sett)
    {
    if (sett.show_all_characters)
      {
      return (character == 9 || character == 10 || character == 13 || character == 32);
      }
    return false;
    }

  uint32_t character_width(uint32_t character, jamlib::encoding enc, const settings& sett)
    {
    switch (character)
      {
      case 9: return sett.tab_space;
      case 10: return sett.show_all_characters ? 2 : 1;
      case 13: return sett.show_all_characters ? 2 : 1;
      default: return 1;
      }
    }

  uint16_t character_to_pdc_char(uint32_t character, uint32_t char_id, jamlib::encoding enc, const settings& sett)
    {
    if (character > 65535)
      return '?';
    switch (character)
      {
      case 9: 
      {
      if (sett.show_all_characters)
        {
        switch (char_id)
          {
          case 0: return 84; break;
          case 1: return 66; break;
          default: return 32; break;
          }
        }
      return 32; break;
      }
      case 10: {
      if (sett.show_all_characters)
        return char_id == 0 ? 76 : 70;
      return 32; break;
      }
      case 13: {
      if (sett.show_all_characters)
        return char_id == 0 ? 67 : 82;
      return 32; break;
      }
      case 32: return sett.show_all_characters ? 46 : 32; break;
      default: return enc == jamlib::ENC_UTF8 ? (uint16_t)character : jamlib::ascii_to_utf16((unsigned char)character);
      }
    }
  }

window::window(int ix, int iy, int icols, int irows, uint32_t fid, uint32_t nid, bool command_window) : file_id(fid),
nephew_id(nid), is_command_window(command_window), file_pos(0), file_col(0), wordwrap_row(0), word_wrap(true), scroll_fraction(0.1),
piped(false), highlight_comments(true), piped_prompt_index(0), previous_file_pos(-1), previous_file_pos_was_comment(false)
  {
#ifdef _WIN32
    process = nullptr;
#endif
  if (command_window)
    word_wrap = true;
  resize(icols, irows);
  move(ix, iy);
  }

window::~window()
  {
  }

void window::kill_pipe()
  {
  if (piped)
    {      
    JAM::destroy_pipe(process, 9);
    piped = false;
  #ifdef _WIN32
    process = nullptr;
  #endif
    }
  }

void window::resize(int icols, int irows)
  {
  outer_cols = icols;
  outer_rows = irows;
  cols = is_command_window ? outer_cols - ICON_LENGTH : outer_cols - OFFSET_FROM_SCROLLBAR;
  rows = outer_rows;
  }

void window::move(int ix, int iy)
  {
  outer_x = ix;
  outer_y = iy;
  x = is_command_window ? outer_x + ICON_LENGTH : outer_x + OFFSET_FROM_SCROLLBAR;
  y = outer_y;
  }

namespace
  {
  std::string int_to_str(int i)
    {
    std::stringstream ss;
    ss << i;
    return ss.str();
    }

  enum window_type
    {
    WT_BODY,
    WT_COMMAND,
    WT_COLUMN,
    WT_TOP
    };

  void set_normal_attr(window_type wt)
    {
    attrset(A_NORMAL);
    switch (wt)
      {
      case WT_BODY: attron(COLOR_PAIR(editor_window)); break;
      case WT_TOP: attron(COLOR_PAIR(top_window)); break;
      case WT_COLUMN: attron(COLOR_PAIR(column_window)); break;
      case WT_COMMAND: attron(COLOR_PAIR(command_window)); break;
      }
    }

  void set_normal_attr(std::vector<chtype>& attribute_stack, window_type wt)
    {
    attrset(A_NORMAL);
    switch (wt)
      {
      case WT_BODY: attron(COLOR_PAIR(editor_window)); attribute_stack.push_back(COLOR_PAIR(editor_window)); break;
      case WT_TOP: attron(COLOR_PAIR(top_window));  attribute_stack.push_back(COLOR_PAIR(top_window)); break;
      case WT_COLUMN: attron(COLOR_PAIR(column_window));  attribute_stack.push_back(COLOR_PAIR(column_window)); break;
      case WT_COMMAND: attron(COLOR_PAIR(command_window));  attribute_stack.push_back(COLOR_PAIR(command_window)); break;
      }
    }

  struct comment_ranges
    {
    std::vector<int64_t> low, high;
    };

  bool is_comment(const comment_ranges& ranges, int64_t pos)
    {
    auto it = std::lower_bound(ranges.high.begin(), ranges.high.end(), pos);
    if (it == ranges.high.end())
      return false;
    size_t i = std::distance(ranges.high.begin(), it);
    int64_t low = ranges.low[i];
    return (pos >= low);
    }

  bool in_quotes(const jamlib::file& f, int64_t p1, int64_t p2)
    {    
    int nr_of_left_quotes = 0;
    auto it = f.content.begin() + p1;
    while (p1 > 0 && (*it != '\n'))
      {
      --p1;
      --it;
      if (*it == '"')        
        ++nr_of_left_quotes;        
      }
    return (nr_of_left_quotes % 2 == 1);
    //if (nr_of_left_quotes % 2 == 0)
    //  return false;
    /*
    it = f.content.begin() + p2;
    while (p2 < f.content.size() && (*it != '\n'))
      {
      ++p2;
      ++it;
      if (*it == '"')
        return true;
      }
      */
    }

  int64_t find_previous_comment_sign(jamlib::app_state state, bool begin, int64_t p1, int64_t p2, const comment_data& cd)
    {    
    const std::string* p_target = (begin ? &cd.multiline_begin : &cd.multiline_end);
    if (state.files[state.active_file].content.size() < p_target->size())
      return -1;

    p2 += p_target->size(); // It's possible that p2 itself is the start of the comment sign, so we have to add the length of the comment sign to p2
    if (p2 > state.files[state.active_file].content.size())
      p2 = state.files[state.active_file].content.size();

    state.files[state.active_file].dot.r.p1 = p1;
    state.files[state.active_file].dot.r.p2 = p2;
    bool found = false;
    while (!found)
      {
      std::stringstream ss;
      ss << "s/" << resolve_regex_escape_characters(*p_target) << "/" << resolve_jamlib_escape_characters(*p_target) << "/";
      state = *jamlib::handle_command(state, ss.str());
      if (state.files[state.active_file].dot.r.p2 - state.files[state.active_file].dot.r.p1 != p_target->size())
        return -1;
      int64_t candidate = state.files[state.active_file].dot.r.p1;

      for (int j = 0; j < p_target->size(); ++j)
        {
        if (state.files[state.active_file].content[candidate + j] != (*p_target)[j])
          return -1;
        }
      /*
      ss << ".-/" << resolve_regex_escape_characters(*p_target) << "/";
      state = *jamlib::handle_command(state, ss.str());
      int64_t candidate = state.files[state.active_file].dot.r.p1;
      if (candidate == 0)
        {
        for (int j = 0; j < p_target->size(); ++j)
          {
          if (state.files[state.active_file].content[j] != (*p_target)[j])
            return -1;
          }
        return 0;
        }
        */
      found = true; // we found a candidate, but have to check whether it is not commented out by the single line comment, or is in quotes

      bool is_quoted = in_quotes(state.files[state.active_file], state.files[state.active_file].dot.r.p1, state.files[state.active_file].dot.r.p2);
      if (is_quoted)
        {
        found = false;
        state.files[state.active_file].dot.r.p1 = p1; // restart search
        state.files[state.active_file].dot.r.p2 = candidate;
        continue;
        }

      if (!begin)
        return candidate;
      int64_t linebegin = candidate;
      auto it = state.files[state.active_file].content.begin() + linebegin;
      while (linebegin && (*it != '\n'))
        {
        --it;
        --linebegin;
        }
      state.files[state.active_file].dot.r.p1 = linebegin;
      state.files[state.active_file].dot.r.p2 = candidate;
      std::stringstream ss2;
      ss2 << "s/" << resolve_regex_escape_characters(cd.single_line) << "/" << resolve_jamlib_escape_characters(cd.single_line) << "/";
      is_quoted = true;
      while (is_quoted)
        {
        is_quoted = false;

        state = *jamlib::handle_command(state, ss2.str()); // find single line comment and substitute by single line comment
        if (state.files[state.active_file].dot.r.p2 - state.files[state.active_file].dot.r.p1 == cd.single_line.size())
          {
          int64_t t = state.files[state.active_file].dot.r.p1;
          found = false;
          is_quoted = in_quotes(state.files[state.active_file], state.files[state.active_file].dot.r.p1, state.files[state.active_file].dot.r.p2);
          if (!is_quoted)
            {
            for (int j = 0; j < cd.single_line.size(); ++j)
              if (state.files[state.active_file].content[t + j] != cd.single_line[j])
                found = true;
            }
          else
            {
            state.files[state.active_file].dot.r.p1 = state.files[state.active_file].dot.r.p2;
            state.files[state.active_file].dot.r.p2 = candidate;
            }
          }
        }
      if (found)
        return candidate;
      state.files[state.active_file].dot.r.p1 = p1; // restart search
      state.files[state.active_file].dot.r.p2 = candidate;
      }
    return -1;
    }

  int64_t find_next_comment_sign(jamlib::app_state state, bool begin, int64_t p1, int64_t p2, const comment_data& cd)
    {
    const std::string* p_target = (begin ? &cd.multiline_begin : &cd.multiline_end);
    if (state.files[state.active_file].content.size() < p_target->size())
      return -1;
    state.files[state.active_file].dot.r.p1 = p1;
    state.files[state.active_file].dot.r.p2 = p2;
    bool found = false;
    while (!found)
      {
      std::stringstream ss;
      ss << "s/" << resolve_regex_escape_characters(*p_target) << "/" << resolve_jamlib_escape_characters(*p_target) << "/";
      state = *jamlib::handle_command(state, ss.str());
      if (state.files[state.active_file].dot.r.p2 - state.files[state.active_file].dot.r.p1 != p_target->size())
        return -1;
      int64_t candidate = state.files[state.active_file].dot.r.p1;

      for (int j = 0; j < p_target->size(); ++j)
        {
        if (state.files[state.active_file].content[candidate + j] != (*p_target)[j])
          return -1;
        }
      found = true; // we found a candidate, but have to check whether it is not commented out by the single line comment or in quotes

      bool is_quoted = in_quotes(state.files[state.active_file], state.files[state.active_file].dot.r.p1, state.files[state.active_file].dot.r.p2);
      if (is_quoted)
        {
        found = false;
        state.files[state.active_file].dot.r.p1 = candidate + p_target->size(); // restart search
        state.files[state.active_file].dot.r.p2 = p2;
        continue;
        }

      if (!begin)
        return candidate;
      int64_t linebegin = candidate;
      auto it = state.files[state.active_file].content.begin() + linebegin;
      while (linebegin && (*it != '\n'))
        {
        --it;
        --linebegin;
        }
      state.files[state.active_file].dot.r.p1 = linebegin;
      state.files[state.active_file].dot.r.p2 = candidate;

      std::stringstream ss2;
      ss2 << "s/" << resolve_regex_escape_characters(cd.single_line) << "/" << resolve_jamlib_escape_characters(cd.single_line) << "/";
      is_quoted = true;
      while (is_quoted)
        {
        is_quoted = false;
        state = *jamlib::handle_command(state, ss2.str()); // find single line comment and substitute by single line comment      
        if (state.files[state.active_file].dot.r.p2 - state.files[state.active_file].dot.r.p1 == cd.single_line.size())
          {
          int64_t t = state.files[state.active_file].dot.r.p1;
          found = false;
          is_quoted = in_quotes(state.files[state.active_file], state.files[state.active_file].dot.r.p1, state.files[state.active_file].dot.r.p2);
          if (!is_quoted)
            {
            for (int j = 0; j < cd.single_line.size(); ++j)
              if (state.files[state.active_file].content[t + j] != cd.single_line[j])
                found = true;
            }
          else
            {
            state.files[state.active_file].dot.r.p1 = state.files[state.active_file].dot.r.p2;
            state.files[state.active_file].dot.r.p2 = candidate;
            }
          }
        }
      if (found)
        return candidate;
      state.files[state.active_file].dot.r.p1 = candidate + p_target->size(); // restart search
      state.files[state.active_file].dot.r.p2 = p2;
      }
    return -1;
    }

  bool compute_pos_is_in_comment(jamlib::app_state state, int64_t pos, const comment_data& cd)
    {
    state.files[state.active_file].dot.r.p1 = state.files[state.active_file].dot.r.p2 = pos;
    int64_t first_comment_start = find_previous_comment_sign(state, true, 0, pos, cd); // << this is the slow step

    if (first_comment_start == -1)
      return false;

    state.files[state.active_file].dot.r.p1 = state.files[state.active_file].dot.r.p2 = first_comment_start;
    //int64_t first_comment_stop = find_next_comment_sign(state, false, first_comment_start, pos + 1, cd);
    int64_t first_comment_stop = find_next_comment_sign(state, false, first_comment_start, pos, cd);
    if (first_comment_stop == -1)
      return true;
    return first_comment_stop > pos;
    }

  bool compute_pos_is_in_comment(jamlib::app_state state, int64_t pos, const comment_data& cd, int64_t previous_pos_computed, bool previous_pos_was_comment)
    {
    if (pos == previous_pos_computed)
      return previous_pos_was_comment;
    if (previous_pos_computed < pos) // look forward
      {
      bool currently_inside_comment = previous_pos_was_comment;
      int64_t current_pos = previous_pos_computed;
      for (;;)
        {
        //int64_t next_comment_token = find_next_comment_sign(state, !currently_inside_comment, current_pos, pos + 1, cd);
        int64_t next_comment_token = find_next_comment_sign(state, !currently_inside_comment, current_pos, pos, cd);
        if (next_comment_token == -1)
          return currently_inside_comment;
        current_pos = next_comment_token;
        currently_inside_comment = !currently_inside_comment;
        }
      }
    else // look backward
      {
      bool currently_inside_comment = previous_pos_was_comment;
      int64_t current_pos = previous_pos_computed;
      for (;;)
        {
        int64_t previous_comment_token = find_previous_comment_sign(state, currently_inside_comment, pos, current_pos, cd);
        if (previous_comment_token == -1)
          return currently_inside_comment;
        current_pos = previous_comment_token;
        currently_inside_comment = !currently_inside_comment;
        }
      }
    return compute_pos_is_in_comment(state, pos, cd);
    }

  bool pos_is_in_comment(const jamlib::app_state& state, int64_t pos, const comment_data& cd, int64_t previous_pos_computed, bool previous_pos_was_comment)
    {
    if (previous_pos_computed < 0)
      return compute_pos_is_in_comment(state, pos, cd);
    if (std::abs(previous_pos_computed - pos) > pos)
      return compute_pos_is_in_comment(state, pos, cd);
    return compute_pos_is_in_comment(state, pos, cd, previous_pos_computed, previous_pos_was_comment);
    }

  comment_ranges find_cpp_multiline_comments(jamlib::app_state state, int64_t p1, int64_t p2, const comment_data& cd, int64_t previous_pos_computed, bool previous_pos_was_comment)
    {
    comment_ranges cr;
    if (cd.multiline_begin.empty())
      return cr;
    bool start_as_comment = pos_is_in_comment(state, p1, cd, previous_pos_computed, previous_pos_was_comment);
    int64_t current_pos = p1;
    if (start_as_comment)
      {
      state.files[state.active_file].dot.r.p1 = state.files[state.active_file].dot.r.p2 = p1;
      int64_t first_comment_stop = find_next_comment_sign(state, false, p1, p2, cd);
      if (first_comment_stop == -1)
        {
        cr.low.push_back(p1);
        cr.high.push_back(p2);
        return cr;
        }
      cr.low.push_back(p1);
      cr.high.push_back(first_comment_stop + 1);
      if (first_comment_stop + 2 >= p2)
        return cr;
      current_pos = first_comment_stop + 2;
      }

    while (current_pos < p2)
      {
      state.files[state.active_file].dot.r.p1 = state.files[state.active_file].dot.r.p2 = current_pos;
      int64_t comment_start = find_next_comment_sign(state, true, current_pos, p2, cd);
      if (comment_start == -1)
        return cr;
      if (comment_start >= p2)
        return cr;
      state.files[state.active_file].dot.r.p1 = state.files[state.active_file].dot.r.p2 = comment_start;
      int64_t comment_stop = find_next_comment_sign(state, false, current_pos, p2, cd);
      if (comment_stop == -1)
        {
        cr.low.push_back(comment_start);
        cr.high.push_back(p2);
        return cr;
        }
      cr.low.push_back(comment_start);
      cr.high.push_back(comment_stop + 1);
      if (comment_stop + 2 >= p2)
        return cr;
      current_pos = comment_stop + 2;
      }
    return cr;
    }

  comment_ranges find_cpp_singleline_comments(jamlib::app_state state, int64_t p1, int64_t p2, const comment_data& cd)
    {
    comment_ranges cr;
    int64_t current_pos = p1;
    while (current_pos < p2)
      {
      state.files[state.active_file].dot.r.p1 = current_pos;
      state.files[state.active_file].dot.r.p2 = p2;

      std::stringstream ss;
      ss << "s/" << resolve_regex_escape_characters(cd.single_line) << "/" << resolve_jamlib_escape_characters(cd.single_line) << "/";
      state = *jamlib::handle_command(state, ss.str()); // find single line comment and substitute by single line comment

      if (state.files[state.active_file].dot.r.p2 - state.files[state.active_file].dot.r.p1 == cd.single_line.size())
        {
        int64_t t = state.files[state.active_file].dot.r.p1;
        for (int j = 0; j < cd.single_line.size(); ++j)
          if (state.files[state.active_file].content[t + j] != cd.single_line[j])
            return cr;

        bool is_quoted = in_quotes(state.files[state.active_file], state.files[state.active_file].dot.r.p1, state.files[state.active_file].dot.r.p2);
        if (is_quoted)
          {
          current_pos = state.files[state.active_file].dot.r.p2;
          continue;
          }

        auto it = state.files[state.active_file].content.begin() + t;
        auto it_end = state.files[state.active_file].content.begin() + p2;
        current_pos = t;
        for (; it != it_end; ++it, ++current_pos)
          {
          if (*it == '\n')
            {
            cr.low.push_back(t);
            cr.high.push_back(current_pos);
            break;
            }
          }
        if (it == it_end)
          {
          cr.low.push_back(t);
          cr.high.push_back(current_pos);
          }

        }
      else
        return cr;
      }

    return cr;
    }

  comment_ranges find_cpp_comments(const jamlib::app_state& state, int64_t p1, int64_t p2, const comment_data& cd, int64_t previous_pos_computed, int64_t current_pos, bool& previous_pos_was_multiline_comment)
    {
    comment_ranges cr = find_cpp_multiline_comments(state, p1, p2, cd, previous_pos_computed, previous_pos_was_multiline_comment);
    previous_pos_was_multiline_comment = is_comment(cr, current_pos); // update boolean: current_pos will become previous_pos_computed.
    if (cr.low.empty())
      {
      return find_cpp_singleline_comments(state, p1, p2, cd);
      }
    comment_ranges out = find_cpp_singleline_comments(state, p1, cr.low[0], cd);
    out.low.push_back(cr.low[0]);
    out.high.push_back(cr.high[0]);
    for (int i = 1; i < cr.low.size(); ++i)
      {
      comment_ranges sl = find_cpp_singleline_comments(state, cr.high[i - 1], cr.low[i], cd);
      out.low.insert(out.low.end(), sl.low.begin(), sl.low.end());
      out.high.insert(out.high.end(), sl.high.begin(), sl.high.end());
      out.low.push_back(cr.low[i]);
      out.high.push_back(cr.high[i]);
      }
    comment_ranges sl = find_cpp_singleline_comments(state, cr.high.back(), p2, cd);
    out.low.insert(out.low.end(), sl.low.begin(), sl.low.end());
    out.high.insert(out.high.end(), sl.high.begin(), sl.high.end());
    return out;
    }

  int64_t find_next(const jamlib::file& f, wchar_t left_sign, wchar_t right_sign, int64_t pos, bool reverse, int64_t p1, int64_t p2, comment_ranges comments)
    {
    if (reverse)
      {
      int cnt = 1;
      auto it = f.content.rbegin() + (f.content.size() - pos - 1);
      auto it_end = f.content.rend();
      for (; it != it_end; ++it, --pos)
        {
        if (pos < p1) // match is not visible in current view
          break;
        if (*it == right_sign && !is_comment(comments, pos))
          ++cnt;
        if (*it == left_sign && !is_comment(comments, pos))
          --cnt;
        if (cnt == 0)
          break;
        }
      if (cnt == 0)
        return pos;
      }
    else
      {
      int cnt = 1;
      auto it = f.content.begin() + pos;
      auto it_end = f.content.end();
      for (; it != it_end; ++it, ++pos)
        {
        if (pos > p2) // match is not visible in current view
          break;
        if (*it == left_sign && !is_comment(comments, pos))
          ++cnt;
        if (*it == right_sign && !is_comment(comments, pos))
          --cnt;
        if (cnt == 0)
          break;
        }
      if (cnt == 0)
        return pos;
      }
    return -1;
    }

  std::set<int64_t> find_highlights(const jamlib::file& f, int64_t p1, int64_t p2, comment_ranges comments)
    {
    std::set<int64_t> h;
    if (f.dot.r.p1 != f.dot.r.p2)
      return h;
    if (f.dot.r.p1 >= f.content.size())
      return h;
    if (f.dot.r.p1 < 0)
      return h;
    if (is_comment(comments, f.dot.r.p1))
      return h;
    wchar_t ch = f.content[f.dot.r.p1];

    switch (ch)
      {
      case '(':
      {
      h.insert(f.dot.r.p1);
      int64_t pos = f.dot.r.p1 + 1;
      int64_t res = find_next(f, '(', ')', pos, false, p1, p2, comments);
      if (res >= 0)
        h.insert(res);
      break;
      }
      case ')':
      {
      h.insert(f.dot.r.p1);
      int64_t pos = f.dot.r.p1 - 1;
      int64_t res = find_next(f, '(', ')', pos, true, p1, p2, comments);
      if (res >= 0)
        h.insert(res);
      break;
      }
      case '{':
      {
      h.insert(f.dot.r.p1);
      int64_t pos = f.dot.r.p1 + 1;
      int64_t res = find_next(f, '{', '}', pos, false, p1, p2, comments);
      if (res >= 0)
        h.insert(res);
      break;
      }
      case '}':
      {
      h.insert(f.dot.r.p1);
      int64_t pos = f.dot.r.p1 - 1;
      int64_t res = find_next(f, '{', '}', pos, true, p1, p2, comments);
      if (res >= 0)
        h.insert(res);
      break;
      }
      case '[':
      {
      h.insert(f.dot.r.p1);
      int64_t pos = f.dot.r.p1 + 1;
      int64_t res = find_next(f, '[', ']', pos, false, p1, p2, comments);
      if (res >= 0)
        h.insert(res);
      break;
      }
      case ']':
      {
      h.insert(f.dot.r.p1);
      int64_t pos = f.dot.r.p1 - 1;
      int64_t res = find_next(f, '[', ']', pos, true, p1, p2, comments);
      if (res >= 0)
        h.insert(res);
      break;
      }
      case '<':
      {
      h.insert(f.dot.r.p1);
      int64_t pos = f.dot.r.p1 + 1;
      int64_t res = find_next(f, '<', '>', pos, false, p1, p2, comments);
      if (res >= 0)
        h.insert(res);
      break;
      }
      case '>':
      {
      h.insert(f.dot.r.p1);
      int64_t pos = f.dot.r.p1 - 1;
      int64_t res = find_next(f, '<', '>', pos, true, p1, p2, comments);
      if (res >= 0)
        h.insert(res);
      break;
      }
      }
    return h;
    }

  bool is_selecting(chtype ch)
    {
    if (ch & A_REVERSE)
      return true;
    if (ch == COLOR_PAIR(selection))
      return true;
    if (ch == COLOR_PAIR(middle_drag))
      return true;
    if (ch == COLOR_PAIR(right_drag))
      return true;
    return false;
    }

  void draw(window& w, jamlib::app_state state, const settings& sett, window_type wt)
    {
    int64_t the_active_file_id = state.active_file;

    if (w.rows <= 0)
      return;
    if (w.cols <= 0)
      return;
    if (w.outer_x + w.outer_cols > SP->cols)
      return;
    if (w.outer_y + w.outer_rows >= SP->lines)
      return;

    std::vector<chtype> attribute_stack;

    int64_t middle_p1 = mouse.middle_drag_start.pos;
    int64_t middle_p2 = mouse.middle_drag_end.pos;
    if (middle_p2 < middle_p1)
      {
      --middle_p1; // toggles pivot point of selection
      std::swap(middle_p2, middle_p1);
      }
    if (mouse.middle_dragging &&
      mouse.middle_drag_start.id == w.file_id &&
      mouse.middle_drag_end.id == w.file_id && middle_p2 >= 0)
      ++middle_p2;

    int64_t right_p1 = mouse.right_drag_start.pos;
    int64_t right_p2 = mouse.right_drag_end.pos;
    if (right_p2 < right_p1)
      {
      --right_p1; // toggles pivot point of selection
      std::swap(right_p2, right_p1);
      }
    if (mouse.right_dragging &&
      mouse.right_drag_start.id == w.file_id &&
      mouse.right_drag_end.id == w.file_id && right_p2 >= 0)
      ++right_p2;

    invalidate_range(w.outer_x, w.outer_y, w.outer_cols, w.outer_rows);
    attrset(A_NORMAL);
    attribute_stack.push_back(A_NORMAL);
    if (w.is_command_window)
      {
      if (wt == WT_COMMAND)
        {
        if (is_modified(state.files[w.nephew_id]))
          attron(COLOR_PAIR(modified_icon));
        else
          attron(COLOR_PAIR(command_window));
        move(w.outer_y, w.outer_x);
        add_ex(w.file_id, -1, SET_ICON);
        addch(jamlib::ascii_to_utf16(175));
        move(w.outer_y, w.outer_x + 1);
        add_ex(w.file_id, -1, SET_ICON);
        addch(jamlib::ascii_to_utf16(175));
        move(w.outer_y, w.outer_x + 2);
        add_ex(w.file_id, -1, SET_ICON);
        addch(jamlib::ascii_to_utf16(175));
        move(w.outer_y, w.outer_x + 3);
        add_ex(w.file_id, 0, SET_TEXT);
        addch(' ');
        attroff(COLOR_PAIR(command_window));
        attroff(COLOR_PAIR(modified_icon));
        attron(COLOR_PAIR(command_window));
        attribute_stack.push_back(COLOR_PAIR(command_window));
        }
      else if (wt == WT_TOP)
        {
        attron(COLOR_PAIR(top_window));
        move(w.outer_y, w.outer_x);
        add_ex(w.file_id, 0, SET_TEXT);
        addch(' ');
        move(w.outer_y, w.outer_x + 1);
        add_ex(w.file_id, 0, SET_TEXT);
        addch(' ');
        move(w.outer_y, w.outer_x + 2);
        add_ex(w.file_id, 0, SET_TEXT);
        addch(' ');
        move(w.outer_y, w.outer_x + 3);
        add_ex(w.file_id, 0, SET_TEXT);
        addch(' ');
        attribute_stack.push_back(COLOR_PAIR(top_window));
        }
      else if (wt == WT_COLUMN)
        {
        attron(COLOR_PAIR(column_window));
        move(w.outer_y, w.outer_x);
        add_ex(w.file_id, -1, SET_ICON);
        addch(jamlib::ascii_to_utf16(174));
        move(w.outer_y, w.outer_x + 1);
        add_ex(w.file_id, -1, SET_ICON);
        addch(jamlib::ascii_to_utf16(174));
        move(w.outer_y, w.outer_x + 2);
        add_ex(w.file_id, -1, SET_ICON);
        addch(jamlib::ascii_to_utf16(174));
        move(w.outer_y, w.outer_x + 3);
        add_ex(w.file_id, 0, SET_TEXT);
        addch(' ');
        attroff(COLOR_PAIR(column_window));
        attron(COLOR_PAIR(column_window));
        attribute_stack.push_back(COLOR_PAIR(column_window));
        }
      }
    else
      {
      attron(COLOR_PAIR(editor_window));
      attribute_stack.push_back(COLOR_PAIR(editor_window));
      }

    chtype dot_format = 0;
    if (state.files[w.file_id].dot.r.p1 == state.files[w.file_id].dot.r.p2)
      {
      if (state.active_file == w.file_id)
        dot_format = A_REVERSE;
      }
    else
      {
      if (wt == WT_BODY)
        dot_format = COLOR_PAIR(selection);
      else
        dot_format = COLOR_PAIR(selection_command);
      }

    bool draw_cursor = false;

    int64_t orig_p1 = state.files[w.file_id].dot.r.p1;
    int64_t orig_p2 = state.files[w.file_id].dot.r.p2;

    if (keyb_data.selecting && keyb_data.selection_id == w.file_id)
      {
      orig_p1 = keyb_data.selection_start;
      orig_p2 = keyb_data.selection_end;
      }

    if (orig_p2 < orig_p1)
      std::swap(orig_p1, orig_p2);

    if (orig_p1 == orig_p2)
      {
      draw_cursor = true;
      ++orig_p2;
      }


    jamlib::file file_for_finding_highlights = state.files[w.file_id];

    state.active_file = w.file_id;

    if (w.is_command_window)
      {
      std::stringstream str;
      state = *jamlib::handle_command(state, ",");
      }
    else
      {
      std::stringstream str;
      str << "#" << w.file_pos << ",#" << w.file_pos << " + " << w.rows + w.wordwrap_row;
      state = *jamlib::handle_command(state, str.str());
      }

    int64_t p1 = state.files[state.active_file].dot.r.p1;
    int64_t p2 = state.files[state.active_file].dot.r.p2;    

    bool this_window_might_have_comments = false;

    comment_ranges comments;
    if (!w.is_command_window && w.highlight_comments)
      {
      auto ext = JAM::get_extension(remove_quotes_from_path(state.files[w.file_id].filename));
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
      comment_data cd;
      if (has_syntax_highlight(cd, ext))
        {
        this_window_might_have_comments = true;
        comments = find_cpp_comments(state, p1, p2, cd, w.previous_file_pos, w.file_pos, w.previous_file_pos_was_comment);
        w.previous_file_pos = w.file_pos;
        }
      else
        {
        std::string fn = JAM::get_filename(remove_quotes_from_path(state.files[w.file_id].filename));
        std::transform(fn.begin(), fn.end(), fn.begin(), [](unsigned char c) { return std::tolower(c); });
        if (has_syntax_highlight(cd, fn))
          {
          this_window_might_have_comments = true;
          comments = find_cpp_comments(state, p1, p2, cd, w.previous_file_pos, w.file_pos, w.previous_file_pos_was_comment);
          w.previous_file_pos = w.file_pos;
          }
        } 
      }

    if (!this_window_might_have_comments)
      {
      w.previous_file_pos = -1;
      w.previous_file_pos_was_comment = false;
      }

    auto highlights = find_highlights(file_for_finding_highlights, p1, p2, comments); // p1 and p2 are lower and upper bounds for finding matches (,) {,}

    int64_t row = 0;
    int64_t col = 0;

    auto it = state.files[state.active_file].content.begin() + p1;
    auto it_end = state.files[state.active_file].content.begin() + p2;

    int64_t pos = p1;

    int64_t first_pos = -1; // will contain position of actual first character, useful for scrollbar    

    if (pos >= orig_p1 && pos < orig_p2)
      {
      attribute_stack.push_back(dot_format);
      attron(dot_format);
      }

    for (; it != it_end; ++it, ++pos)
      {
      assert(it == state.files[state.active_file].content.begin() + pos);
      if (w.is_command_window && (col == 0) && (row > 0))
        {
        set_normal_attr(attribute_stack, wt);
        for (int k = 0; k < ICON_LENGTH; ++k)
          {
          move(w.outer_y + (int)row, w.outer_x + k);
          add_ex(w.file_id, -1, SET_TEXT);
          addch(' ');
          }
        attroff(attribute_stack.back());
        attribute_stack.pop_back();
        attron(attribute_stack.back());
        }

      if (mouse.middle_dragging &&
        mouse.middle_drag_start.id == w.file_id &&
        mouse.middle_drag_end.id == w.file_id &&
        ((pos == middle_p1) || (pos == middle_p2)))
        {
        if (pos == middle_p1)
          {
          attroff(attribute_stack.back());
          attribute_stack.push_back(COLOR_PAIR(middle_drag));
          attron(attribute_stack.back());
          }
        else
          {
          attroff(attribute_stack.back());
          attribute_stack.pop_back();
          attron(attribute_stack.back());
          }
        }

      if (mouse.right_dragging &&
        mouse.right_drag_start.id == w.file_id &&
        mouse.right_drag_end.id == w.file_id &&
        ((pos == right_p1) || (pos == right_p2)))
        {
        if (pos == right_p1)
          {
          attroff(attribute_stack.back());
          attribute_stack.push_back(COLOR_PAIR(right_drag));
          attron(attribute_stack.back());
          }
        else
          {
          attroff(attribute_stack.back());
          attribute_stack.pop_back();
          attron(attribute_stack.back());
          }
        }

      move((int)(w.y + (row - w.wordwrap_row)), (int)(w.x + (col - w.file_col)));

      if (pos == orig_p1)
        {
        if (attribute_stack.back() == COLOR_PAIR(middle_drag) || attribute_stack.back() == COLOR_PAIR(right_drag))
          {
          ++orig_p1;
          if (orig_p1 >= orig_p2)
            --orig_p1;
          }
        else if (attribute_stack.back() != dot_format)
          {
          attribute_stack.push_back(dot_format);
          attron(attribute_stack.back());
          }
        }
      if (pos == orig_p2)
        {
        if (attribute_stack.back() == dot_format)
          {
          attroff(attribute_stack.back());
          attribute_stack.pop_back();
          attron(attribute_stack.back());
          }
        else if (attribute_stack.size() > 1 && attribute_stack[attribute_stack.size() - 2] == dot_format)
          {
          auto last = attribute_stack.back();
          attribute_stack.pop_back();
          attribute_stack.pop_back();
          attribute_stack.push_back(last);
          }
        }
      if (*it == '\n')
        {
        if (w.word_wrap)
          {
          if (col >= w.file_col && row >= w.wordwrap_row)
            {
            //add_ex(w.file_id, pos, SET_TEXT);
            //addch(' ');
            //++col;
            bool ch_invert = character_invert('\n', sett);
            if (ch_invert)
              attron(ALL_CHARS_FORMAT);
            uint32_t cwidth = character_width('\n', state.files[w.file_id].enc, sett);
            for (uint32_t cnt = 0; cnt < cwidth; ++cnt)
              {
              add_ex(w.file_id, pos, SET_TEXT);
              addch(character_to_pdc_char('\n', cnt, state.files[w.file_id].enc, sett));
              }
            if (ch_invert)
              {
              attroff(ALL_CHARS_FORMAT);
              attron(attribute_stack.back());
              }
            col += cwidth;
            }
          }
        else
          {
          if (col >= w.file_col && col < (w.cols + w.file_col) && row < w.rows)
            {
            //add_ex(w.file_id, pos, SET_TEXT);
            //addch(' ');
            //++col;
            bool ch_invert = character_invert('\n', sett);
            if (ch_invert)
              attron(ALL_CHARS_FORMAT);
            uint32_t cwidth = character_width('\n', state.files[w.file_id].enc, sett);
            for (uint32_t cnt = 0; cnt < cwidth; ++cnt)
              {
              add_ex(w.file_id, pos, SET_TEXT);
              addch(character_to_pdc_char('\n', cnt, state.files[w.file_id].enc, sett));
              }
            if (ch_invert)
              {
              attroff(ALL_CHARS_FORMAT);
              attron(attribute_stack.back());
              }
            col += cwidth;
            }
          }
        if (w.is_command_window)
          {
          set_normal_attr(attribute_stack, wt);
          while (col < w.cols) // fill rest of row with color
            {
            add_ex(w.file_id, -1, SET_TEXT);
            addch(' ');
            ++col;
            }
          attroff(attribute_stack.back());
          attribute_stack.pop_back();
          attron(attribute_stack.back());
          }
        col = 0;
        ++row;
        }
      else
        {
        if (w.word_wrap)
          {
          if (col >= w.file_col && row >= w.wordwrap_row)
            {
            if (first_pos < 0)
              first_pos = pos;
            auto character = *it;
            bool should_show_as_comment = is_comment(comments, pos);
            if (should_show_as_comment)
              {
              if (is_selecting(attribute_stack.back()))
                should_show_as_comment = false;
              }
            bool should_highlight = highlights.find(pos) != highlights.end();
            if (should_highlight)
              should_show_as_comment = false;
            if (should_show_as_comment)
              attron(COLOR_PAIR(comment));
            if (should_highlight)
              attron(COLOR_PAIR(highlight));
            bool ch_invert = character_invert(character, sett);
            if (ch_invert)
              attron(ALL_CHARS_FORMAT);
            uint32_t cwidth = character_width(character, state.files[w.file_id].enc, sett);
            for (uint32_t cnt = 0; cnt < cwidth; ++cnt)
              {
              add_ex(w.file_id, pos, SET_TEXT);
              addch(character_to_pdc_char(character, cnt, state.files[w.file_id].enc, sett));
              }
            if (ch_invert)
              {
              attroff(ALL_CHARS_FORMAT);
              attron(attribute_stack.back());
              }
            if (should_highlight)
              {
              attroff(COLOR_PAIR(highlight));
              attron(attribute_stack.back());
              }
            if (should_show_as_comment)
              {
              attroff(COLOR_PAIR(comment));
              attron(attribute_stack.back());
              }
            col += cwidth;
            }
          else
            ++col;
          if (col >= w.cols - 1)
            {
            if (w.is_command_window)
              {
              set_normal_attr(attribute_stack, wt);
              add_ex(w.file_id, -1, SET_TEXT);
              addch(' ');
              attroff(attribute_stack.back());
              attribute_stack.pop_back();
              attron(attribute_stack.back());
              }
            col = 0;
            ++row;
            }
          }
        else
          {
          if (col >= w.file_col && col < (w.cols + w.file_col) && row < w.rows)
            {
            if (first_pos < 0)
              first_pos = pos;
            auto character = *it;
            bool should_show_as_comment = is_comment(comments, pos);
            if (should_show_as_comment)
              {
              if (is_selecting(attribute_stack.back()))
                should_show_as_comment = false;
              }
            bool should_highlight = highlights.find(pos) != highlights.end();
            if (should_highlight)
              should_show_as_comment = false;
            if (should_show_as_comment)
              attron(COLOR_PAIR(comment));
            if (should_highlight)
              attron(COLOR_PAIR(highlight));
            bool ch_invert = character_invert(character, sett);
            if (ch_invert)
              attron(ALL_CHARS_FORMAT);
            uint32_t cwidth = character_width(character, state.files[w.file_id].enc, sett);
            for (uint32_t cnt = 0; cnt < cwidth; ++cnt)
              {
              add_ex(w.file_id, pos, SET_TEXT);
              addch(character_to_pdc_char(character, cnt, state.files[w.file_id].enc, sett));
              }
            if (ch_invert)
              {
              attroff(ALL_CHARS_FORMAT);
              attron(attribute_stack.back());
              }
            if (should_highlight)
              {
              attroff(COLOR_PAIR(highlight));
              attron(attribute_stack.back());
              }
            if (should_show_as_comment)
              {
              attroff(COLOR_PAIR(comment));
              attron(attribute_stack.back());
              }
            col += cwidth;
            }
          else
            ++col;
          }

        }
      if (row - w.wordwrap_row >= w.rows)
        break;
      }

    if (w.is_command_window && (col == 0) && (row > 0))
      {
      set_normal_attr(attribute_stack, wt);
      for (int k = 0; k < ICON_LENGTH; ++k)
        {
        move(w.outer_y + (int)row, w.outer_x + k);
        add_ex(w.file_id, -1, SET_TEXT);
        addch(' ');
        }
      attroff(attribute_stack.back());
      attribute_stack.pop_back();
      attron(attribute_stack.back());
      }

    set_normal_attr(wt);
    if (it_end == state.files[state.active_file].content.end())
      {
      if (orig_p1 == state.files[state.active_file].content.size())
        attron(dot_format);
      move((int)(w.y + (row - w.wordwrap_row)), (int)(w.x + (col - w.file_col)));
      if (w.word_wrap)
        {
        if (col >= w.file_col && row >= w.wordwrap_row)
          {
          add_ex(w.file_id, pos, SET_TEXT);
          addch(' ');
          }
        }
      else
        {
        if (col >= w.file_col && col < (w.cols + w.file_col) && row < w.rows)
          {
          add_ex(w.file_id, pos, SET_TEXT);
          addch(' ');
          }
        }
      ++col;
      }

    if (first_pos < 0)
      first_pos = p1;

    set_normal_attr(wt);
    if (w.is_command_window)
      {
      attroff(COLOR_PAIR(middle_drag));
      attroff(COLOR_PAIR(right_drag));
      if (wt == WT_COMMAND)
        attron(COLOR_PAIR(command_window));
      switch (wt)
        {
        case WT_COMMAND: if (w.nephew_id == the_active_file_id) attron(COLOR_PAIR(active_window)); else attron(COLOR_PAIR(command_window)); break;
        case WT_TOP:attron(COLOR_PAIR(top_window)); break;
        case WT_COLUMN:attron(COLOR_PAIR(column_window)); break;
        default: assert(0); break;
        }
      while (col < w.cols)
        {
        move((int)(w.y + (row - w.wordwrap_row)), (int)(w.x + (col - w.file_col)));
        addch(' ');
        ++col;
        }

      for (int by = 0; by < COMMAND_BORDER_SIZE; ++by)
        {
        col = -ICON_LENGTH;
        move((int)(w.y + by + 1 + row), (int)(w.x + col));
        add_ex(w.file_id, -1, -1);
        addch('.');
        ++col;
        while (col < w.cols - 1)
          {
          move((int)(w.y + by + 1 + row), (int)(w.x + col));
          add_ex(w.file_id, -1, -1);
          addch('_');
          ++col;
          }
        move((int)(w.y + by + 1 + row), (int)(w.x + col));
        add_ex(w.file_id, -1, -1);
        addch('.');
        }
      attroff(COLOR_PAIR(active_window));
      attroff(COLOR_PAIR(command_window));
      attroff(COLOR_PAIR(top_window));
      attroff(COLOR_PAIR(column_window));
      }
    else
      attroff(COLOR_PAIR(editor_window));

    if (!w.is_command_window)
      {
      int scroll1 = 0;
      int scroll2 = (w.rows - 2);

      if (!state.files[state.active_file].content.empty())
        {
        scroll1 = (int)((double)first_pos / (double)state.files[state.active_file].content.size() * (w.rows - 2));
        scroll2 = (int)((double)pos / (double)state.files[state.active_file].content.size() * (w.rows - 2));
        }

      const unsigned char scrollbar_ascii_sign = 219;

      if (scroll1 == 0)
        attron(COLOR_PAIR(scroll_bar));
      else
        attron(COLOR_PAIR(scroll_bar_2));
      move(w.y, w.outer_x);
      add_ex(w.file_id, 0, SET_SCROLLBAR);
      //addch(ACS_ULCORNER);
      //addch(jamlib::ascii_to_utf16(201));
      addch(jamlib::ascii_to_utf16(scrollbar_ascii_sign));
      move(w.y, w.outer_x + 1);
      add_ex(w.file_id, 0, SET_SCROLLBAR);
      for (int r = 1; r < w.rows - 1; ++r)
        {
        //int64_t scrollbar_pos = 0;
        //if (w.rows - 2 - nr_of_scroll_rows > 0)
        //  scrollbar_pos = (int64_t)(state.files[state.active_file].content.size()*std::min<double>(1.0, (r - 1) / (double)(w.rows - 2 - nr_of_scroll_rows)));
        move(w.y + r, w.outer_x);
        int64_t scrollbar_pos = (int64_t)((double)(r - 1) / (double)(w.rows - 2) * (double)state.files[state.active_file].content.size());
        add_ex(w.file_id, scrollbar_pos, SET_SCROLLBAR);
        //if (r - 1 >= scroll_pos && r - 1 < scroll_pos + nr_of_scroll_rows)
        if (r - 1 == scroll1)
          {
          attroff(COLOR_PAIR(scroll_bar_2));
          attron(COLOR_PAIR(scroll_bar));
          }
        //addch(jamlib::ascii_to_utf16(186));
        addch(jamlib::ascii_to_utf16(scrollbar_ascii_sign));
        if (r - 1 == scroll2)
          {
          attroff(COLOR_PAIR(scroll_bar));
          attron(COLOR_PAIR(scroll_bar_2));
          }
        //else
        //  addch(ACS_VLINE);
        move(w.y + r, w.outer_x + 1);
        add_ex(w.file_id, scrollbar_pos, SET_SCROLLBAR);
        }
      if (scroll2 == w.rows - 2)
        {
        attroff(COLOR_PAIR(scroll_bar_2));
        attron(COLOR_PAIR(scroll_bar));
        }
      move(w.y + w.rows - 1, w.outer_x);
      add_ex(w.file_id, state.files[state.active_file].content.size(), SET_SCROLLBAR);
      //addch(ACS_LLCORNER);
      //addch(jamlib::ascii_to_utf16(200));
      addch(jamlib::ascii_to_utf16(scrollbar_ascii_sign));
      move(w.y + w.rows - 1, w.outer_x + 1);
      add_ex(w.file_id, state.files[state.active_file].content.size(), SET_SCROLLBAR);
      attroff(COLOR_PAIR(scroll_bar));
      }
    }

  }

window_pair::window_pair(int ix, int iy, int icols, int irows, uint32_t wid, uint32_t cwid) :
  x(ix), y(iy), cols(icols), rows(irows), window_id(wid),
  command_window_id(cwid)
  {
  }

window_pair::~window_pair()
  {

  }

void window_pair::resize(int icols, int irows)
  {
  cols = icols;
  rows = irows;
  }

void window_pair::move(int ix, int iy)
  {
  x = ix;
  y = iy;
  }

jamlib::range get_window_range(window w, jamlib::app_state state)
  {
  std::stringstream str;
  state.active_file = w.file_id;
  str << "#" << w.file_pos << ", #" << w.file_pos << "+" << w.rows;
  state = *jamlib::handle_command(state, str.str());
  return state.files[w.file_id].dot.r;
  }

std::vector<window> draw(const grid& g, const std::vector<window_pair>& window_pairs, std::vector<window> windows, jamlib::app_state state, const settings& sett)
  {
  for (const auto& c : g.columns)
    {
    window& column_win = windows[c.column_command_window_id];
    draw(column_win, state, sett, WT_COLUMN);

    for (const auto& ci : c.items)
      {
      const auto& wp = window_pairs[ci.window_pair_id];
      if (wp.rows == 0)
        continue;
      window& cw = windows[wp.command_window_id];
      cw.file_pos = 0;
      cw.file_col = 0;
      cw.wordwrap_row = 0;

      auto cw_size = state.files[cw.file_id].content.size();

      cw.resize(wp.cols, wp.cols > ICON_LENGTH + 1 ? cw_size / (wp.cols - 1 - ICON_LENGTH) + 1 + COMMAND_BORDER_SIZE : 0);
      cw.move(wp.x, wp.y);
      draw(cw, state, sett, WT_COMMAND);

      window& fw = windows[wp.window_id];
      fw.resize(wp.cols, wp.rows - cw.rows);
      fw.move(wp.x, wp.y + cw.rows);
      draw(fw, state, sett, WT_BODY);
      }
    }

  window& topw = windows[g.topline_window_id];
  draw(topw, state, sett, WT_TOP);

  return windows;
  }


void save_window_to_stream(std::ostream& str, const window& w)
  {
  str << w.outer_x << std::endl;
  str << w.outer_y << std::endl;
  str << w.outer_cols << std::endl;
  str << w.outer_rows << std::endl;
  str << w.x << std::endl;
  str << w.y << std::endl;
  str << w.cols << std::endl;
  str << w.rows << std::endl;
  str << w.file_id << std::endl;
  str << w.nephew_id << std::endl;
  str << w.is_command_window << std::endl;
  str << w.highlight_comments << std::endl;
  str << w.piped << std::endl;
  str << w.file_pos << std::endl;
  str << w.file_col << std::endl;
  str << w.wordwrap_row << std::endl;
  str << w.word_wrap << std::endl;
  str << w.scroll_fraction << std::endl;
  str << w.previous_file_pos << std::endl;
  str << w.previous_file_pos_was_comment << std::endl;
  }

window load_window_from_stream(std::istream& str)
  {
  window w(0, 0, 0, 0, 0, 0, false);
  str >> w.outer_x >> w.outer_y >> w.outer_cols >> w.outer_rows;
  str >> w.x >> w.y >> w.cols >> w.rows;
  str >> w.file_id >> w.nephew_id;
  str >> w.is_command_window >> w.highlight_comments >> w.piped >> w.file_pos >> w.file_col;
  str >> w.wordwrap_row >> w.word_wrap >> w.scroll_fraction;
  str >> w.previous_file_pos >> w.previous_file_pos_was_comment;
  return w;
  }


void save_window_pair_to_stream(std::ostream& str, const window_pair& w)
  {
  str << w.window_id << std::endl;
  str << w.command_window_id << std::endl;
  str << w.x << std::endl;
  str << w.y << std::endl;
  str << w.cols << std::endl;
  str << w.rows << std::endl;
  }

window_pair load_window_pair_from_stream(std::istream& str)
  {
  window_pair w(0, 0, 0, 0, 0, 0);

  str >> w.window_id >> w.command_window_id;
  str >> w.x >> w.y >> w.cols >> w.rows;
  return w;
  }