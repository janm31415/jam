#include "jam.h"
#include "parse.h"
#include "pipe.h"
#include "error.h"
#include <fstream>
#include <iostream>
#include <optional>
#include <unordered_map>
#include <variant>
#include <sstream>
#include <regex>
#include <thread>

#include <utils/utf8.h>
#include <utils/filename.h>

namespace jamlib
  {

  namespace
    {

    static std::wostream* gp_jamlib_output = &std::wcout;

    file make_empty_file(uint64_t file_id)
      {
      file out;
      out.dot.r.p1 = out.dot.r.p2 = 0;
      out.dot.file_id = file_id;
      out.file_id = file_id;
      out.modification_mask = 0;
      out.undo_redo_index = 0;
      out.enc = ENC_UTF8;
      return out;
      }


    bool file_exists(const std::string& filename)
      {
#ifdef _WIN32
      std::wstring wfilename = convert_string_to_wstring(filename, ENC_UTF8); // filenames are in utf8 encoding
      std::wifstream f;
      f.open(wfilename, std::wifstream::in);
#else
      std::ifstream f;
      f.open(filename, std::ifstream::in);  
#endif
      if (f.fail())
        return false;
      f.close();
      return true;
      }

    buffer read_buffer_from_file(const std::string& filename, encoding& enc)
      {
      buffer b;
      if (file_exists(filename))
        {
      #ifdef _WIN32
        std::wstring wfilename = convert_string_to_wstring(filename, ENC_UTF8); // filenames are in utf8 encoding
      #else
        std::string wfilename(filename);
      #endif
        auto cont = b.transient();
        if (enc == ENC_UTF8 && JAM::valid_utf8_file(wfilename))
          {
          auto f = std::ifstream{ wfilename };
          std::string file_in_chars;
          {
          std::stringstream ss;
          ss << f.rdbuf();
          file_in_chars = ss.str();
          }
          auto it = file_in_chars.begin();
          auto it_end = file_in_chars.end();
          utf8::utf8to16(it, it_end, std::back_inserter(cont));
          f.close();
          }
        else
          {
          if (enc == ENC_UTF8)
            enc = ENC_ASCII;
          auto f = std::ifstream{ wfilename };
          std::string line;
          while (std::getline(f, line))
            {
            for (const auto& ch : line)
              {
              switch (enc)
                {
                case ENC_ASCII: cont.push_back(ch); break;
                default: cont.push_back(ch); break;
                }
              }
            if (!f.eof())
              cont.push_back('\n');
            }
          f.close();
          }
        b = cont.persistent();
        }
      return b;
      }

    file read_file(const std::string& filename, uint64_t file_id)
      {
      file out = make_empty_file(file_id);
      out.filename = filename;
      out.content = read_buffer_from_file(filename, out.enc);
      out.dot.r.p1 = 0;
      out.dot.r.p2 = out.content.size();
      return out;
      }

    address interpret_address_range(const AddressRange& addr, file f);

    uint64_t get_line_number(int64_t pos, buffer b)
      {
      uint64_t ln = 1;
      int64_t chars_read = 0;
      auto it = b.begin();
      auto it_end = b.end();
      for (; it != it_end; ++it, ++chars_read)
        {
        if (chars_read == pos)
          return ln;
        if (*it == '\n')
          ++ln;
        }
      return ln;
      }


    int64_t find_next_line_position(int64_t pos, file f)
      {
      auto it = f.content.begin() + pos;
      auto it_end = f.content.end();
      for (; it != it_end; ++it, ++pos)
        {
        if (*it == '\n')
          return pos + 1;
        }
      return pos;
      }

    int64_t find_previous_end_of_line_position(int64_t pos, file f)
      {
      auto it = f.content.rbegin() + (f.content.size() - pos);
      auto it_end = f.content.rend();
      for (; it != it_end; ++it, --pos)
        {
        if (*it == '\n')
          return pos - 1;
        }
      return 0;
      }

    range find_regex_range(std::string re, buffer b, bool reverse, int64_t starting_pos, encoding enc)
      {
      range r;      

      if (reverse)
        {
        r.p1 = r.p2 = 0;
        std::basic_regex<wchar_t> reg(convert_string_to_wstring(re, enc));
        auto it = b.begin();
        auto it_end = b.begin() + starting_pos;
        auto exprs_begin = std::regex_iterator<immutable::vector_iterator<wchar_t, false, 5>>(it, it_end, reg);
        auto exprs_end = std::regex_iterator<immutable::vector_iterator<wchar_t, false, 5>>();

        if (exprs_begin != exprs_end)
          {
          std::regex_iterator<immutable::vector_iterator<wchar_t, false, 5>> prev_it = exprs_begin;
          while (exprs_begin != exprs_end)
            {
            prev_it = exprs_begin;
            ++exprs_begin;
            }
          std::match_results<immutable::vector_iterator<wchar_t, false, 5>> last_match = *prev_it;
          r.p1 = std::distance(b.begin(), last_match[0].first);
          r.p2 = std::distance(b.begin(), last_match[0].second);
          }
        }
      else
        {
        r.p1 = r.p2 = b.size();
        std::basic_regex<wchar_t> reg(convert_string_to_wstring(re, enc));
        std::match_results<immutable::vector_iterator<wchar_t, false, 5>> reg_match;
        auto it = b.begin() + starting_pos;
        auto it_end = b.end();
        if (std::regex_search(it, it_end, reg_match, reg))
          {
          r.p1 = std::distance(b.begin(), reg_match[0].first);
          r.p2 = std::distance(b.begin(), reg_match[0].second);
          }
        }
      return r;
      }

    std::wstring get_wide(const std::string& filename)
      {
      std::wstring wfilename = convert_string_to_wstring(filename, ENC_UTF8);
      return wfilename;
      }

    std::string get_utf8(const std::wstring& txt)
      {
      return convert_wstring_to_string(txt, ENC_UTF8);
      }

    std::string get_folder(const std::string& path)
      {
      return JAM::get_folder(path);
      }

    std::string get_filename(const std::string& path)
      {
      return JAM::get_filename(path);
      }    

    struct command_handler
      {
      app_state state;
      bool save_undo;

      command_handler(app_state i_state) : state(i_state), save_undo(true) {}

      void push_undo(snapshot ss)
        {
        if (save_undo)
          {
          state.files[state.active_file].history = state.files[state.active_file].history.push_back(ss);
          state.files[state.active_file].undo_redo_index = state.files[state.active_file].history.size();
          }
        }

      std::optional<app_state> operator() (const Cmd_external& cmd)
        {
        std::string executable_name;
        std::string folder;
        std::string parameters;
        parse_command(executable_name, folder, parameters, cmd.command);

        auto command_line = executable_name + " " + parameters;

        void* process = nullptr;
        int err = StartChildProcessWithoutPipes(command_line.c_str(), folder.c_str(), nullptr, &process);
        if (err != 0)
          throw_error(pipe_error, "Could not create child process");
        DestroyChildProcess(process, 0);
        return state;
        }

      std::optional<app_state> operator() (const Cmd_external_input& cmd)
        {
        std::string executable_name;
        std::string folder;
        std::string parameters;
        parse_command(executable_name, folder, parameters, cmd.command);

        auto command_line = executable_name + " " + parameters;

        void* process = nullptr;
        int err = StartChildProcess(command_line.c_str(), folder.c_str(), nullptr, &process);
        if (err != 0)
          throw_error(pipe_error, "Could not create child process");

        std::string text = ReadFromProgram(process);        

        file f = state.files[state.active_file];

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        buffer txt;
        auto tr = txt.transient();
        auto wtext = convert_string_to_wstring(text, f.enc);
        wtext.erase(std::remove(wtext.begin(), wtext.end(), '\r'), wtext.end());
        for (auto ch : wtext)
          {
          tr.push_back(ch);
          }
        txt = tr.persistent();

        f.content = f.content.erase((uint32_t)f.dot.r.p1, (uint32_t)f.dot.r.p2);

        state.files[state.active_file].content = f.content.insert((uint32_t)f.dot.r.p1, txt);
        state.files[state.active_file].dot.r.p2 = state.files[state.active_file].dot.r.p1 + wtext.length();
        state.files[state.active_file].modification_mask |= 1;
        push_undo(ss);
        DestroyChildProcess(process, 10);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_external_output& cmd)
        {
        std::string executable_name;
        std::string folder;
        std::string parameters;
        parse_command(executable_name, folder, parameters, cmd.command);

        auto command_line = executable_name + " " + parameters;

        void* process = nullptr;
        int err = StartChildProcess(command_line.c_str(), folder.c_str(), nullptr, &process);
        if (err != 0)
          throw_error(pipe_error, "Could not create child process");

        const auto& f = state.files[state.active_file];
        auto it = f.content.begin() + f.dot.r.p1;
        auto it_end = f.content.begin() + f.dot.r.p2;

        std::wstring wstr(it, it_end);
        wstr.erase(std::remove(wstr.begin(), wstr.end(), '\r'), wstr.end());
        auto message = convert_wstring_to_string(wstr, f.enc);

        SendToProgram(message.c_str(), process);

        DestroyChildProcess(process, 10);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_external_io& cmd)
        {
        std::string executable_name;
        std::string folder;
        std::string parameters;
        parse_command(executable_name, folder, parameters, cmd.command);

        auto command_line = executable_name + " " + parameters;

        void* process = nullptr;
        int err = StartChildProcess(command_line.c_str(), folder.c_str(), nullptr, &process);
        if (err != 0)
          throw_error(pipe_error, "Could not create child process");

        file f = state.files[state.active_file];
        auto it = f.content.begin() + f.dot.r.p1;
        auto it_end = f.content.begin() + f.dot.r.p2;

        std::wstring wstr(it, it_end);
        wstr.erase(std::remove(wstr.begin(), wstr.end(), '\r'), wstr.end());
        auto message = convert_wstring_to_string(wstr, f.enc);

        SendToProgram(message.c_str(), process);
    
        std::string text = ReadFromProgram(process);

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        buffer txt;
        auto tr = txt.transient();
        auto wtext = convert_string_to_wstring(text, f.enc);
        wtext.erase(std::remove(wtext.begin(), wtext.end(), '\r'), wtext.end());
        for (auto ch : wtext)
          {
          tr.push_back(ch);
          }
        txt = tr.persistent();

        f.content = f.content.erase((uint32_t)f.dot.r.p1, (uint32_t)f.dot.r.p2);

        state.files[state.active_file].content = f.content.insert((uint32_t)f.dot.r.p1, txt);
        state.files[state.active_file].dot.r.p2 = state.files[state.active_file].dot.r.p1 + wtext.length();
        state.files[state.active_file].modification_mask |= 1;
        push_undo(ss);
        DestroyChildProcess(process, 10);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_a& cmd)
        {
        file f = state.files[state.active_file];

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        buffer txt;
        auto tr = txt.transient();
        auto wtext = convert_string_to_wstring(cmd.txt.text, f.enc);
        for (auto ch : wtext)
          {
          tr.push_back(ch);
          }
        txt = tr.persistent();

        state.files[state.active_file].content = f.content.insert((uint32_t)f.dot.r.p2, txt);
        state.files[state.active_file].dot.r.p1 = f.dot.r.p2;
        state.files[state.active_file].dot.r.p2 = state.files[state.active_file].dot.r.p1 + wtext.length();
        state.files[state.active_file].modification_mask |= 1;
        push_undo(ss);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_A&)
        {
        // convert utf-8 to ascii
        file f = state.files[state.active_file];
        if (f.enc == ENC_ASCII)
          return state;

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        int64_t p1, p2;
        p1 = p2 = 0;
        buffer b;
        auto bt = b.transient();
        int64_t p = 0;
        for (auto wch : f.content)
          {
          if (p == f.dot.r.p1)
            p1 = bt.size();
          if (p == f.dot.r.p2)
            p2 = bt.size();
          std::wstring ws;
          ws.push_back(wch);
          auto s = convert_wstring_to_string(ws, f.enc);
          for (auto ch : s)
            bt.push_back(ch);
          ++p;
          }
        if (p == f.dot.r.p1)
          p1 = bt.size();
        if (p == f.dot.r.p2)
          p2 = bt.size();

        b = bt.persistent();
        f.content = b;
        f.dot.r.p1 = p1;
        f.dot.r.p2 = p2;
        f.enc = ENC_ASCII;
        f.modification_mask |= 1;

        state.files[state.active_file] = f;

        push_undo(ss);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_U&)
        {
        // convert ascii to utf-8
        file f = state.files[state.active_file];
        if (f.enc == ENC_UTF8)
          return state;

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        int64_t p1, p2;
        p1 = p2 = 0;
        buffer b;
        auto bt = b.transient();
        int64_t p = 0;
        auto it = f.content.begin();
        auto it_end = f.content.end();
        for (; it != it_end; )
          {
          uint32_t cp = 0;
          auto first = it;
          utf8::internal::utf_error err_code = utf8::internal::validate_next(it, it_end, cp);
          int64_t p0 = p;
          p += (it - first);
          if (f.dot.r.p1 >= p0 && f.dot.r.p1 < p)
            p1 = bt.size();
          if (f.dot.r.p2 >= p0 && f.dot.r.p2 < p)
            p2 = bt.size();
          if (err_code == utf8::internal::UTF8_OK)
            {
            bt.push_back((wchar_t)cp);
            }
          else
            {
            return state; // not a valid utf-8 file
            }
          }
        if (p == f.dot.r.p1)
          p1 = bt.size();
        if (p == f.dot.r.p2)
          p2 = bt.size();

        b = bt.persistent();
        f.content = b;
        f.dot.r.p1 = p1;
        f.dot.r.p2 = p2;
        f.enc = ENC_UTF8;
        f.modification_mask |= 1;

        state.files[state.active_file] = f;

        push_undo(ss);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_b& cmd)
        {
        if (cmd.value < state.files.size())
          state.active_file = cmd.value;
        return state;
        }

      std::optional<app_state> operator() (const Cmd_c& cmd)
        {
        file f = state.files[state.active_file];

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        buffer txt;
        auto tr = txt.transient();
        auto wtext = convert_string_to_wstring(cmd.txt.text, f.enc);
        for (auto ch : wtext)
          {
          tr.push_back(ch);
          }
        txt = tr.persistent();

        f.content = f.content.erase((uint32_t)f.dot.r.p1, (uint32_t)f.dot.r.p2);

        state.files[state.active_file].content = f.content.insert((uint32_t)f.dot.r.p1, txt);
        state.files[state.active_file].dot.r.p2 = state.files[state.active_file].dot.r.p1 + wtext.length();
        state.files[state.active_file].modification_mask |= 1;
        push_undo(ss);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_d&)
        {
        file f = state.files[state.active_file];

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        state.files[state.active_file].content = f.content.erase((uint32_t)f.dot.r.p1, (uint32_t)f.dot.r.p2);
        state.files[state.active_file].dot.r.p2 = state.files[state.active_file].dot.r.p1;
        state.files[state.active_file].modification_mask |= 1;
        push_undo(ss);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_g& cmd)
        {
        try
          {
          file f = state.files[state.active_file];

          snapshot ss;
          ss.content = f.content;
          ss.dot = f.dot;
          ss.modification_mask = f.modification_mask;
          ss.enc = f.enc;

          //std::regex reg(cmd.regexp.regexp, std::regex::egrep);
          //std::regex reg(cmd.regexp.regexp);
          std::basic_regex<wchar_t> reg(convert_string_to_wstring(cmd.regexp.regexp, f.enc));

          bool save_undo_backup = save_undo;

          save_undo = false;

          auto it = state.files[state.active_file].content.begin() + f.dot.r.p1;
          auto it_end = state.files[state.active_file].content.begin() + f.dot.r.p2;

          std::match_results<immutable::vector_iterator<wchar_t, false, 5>> reg_match;
          if (std::regex_search(it, it_end, reg_match, reg))
            {
            auto new_state = std::visit(*this, cmd.cmd.front());
            if (!new_state)
              return new_state;
            state = *new_state;
            }
          save_undo = save_undo_backup;

          push_undo(ss);
          }
        catch (std::regex_error e)
          {
          throw_error(invalid_regex, e.what());
          }
        return state;
        }

      std::optional<app_state> operator() (const Cmd_i& cmd)
        {
        file f = state.files[state.active_file];

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        buffer txt;
        auto tr = txt.transient();
        auto wtext = convert_string_to_wstring(cmd.txt.text, f.enc);
        for (auto ch : wtext)
          {
          tr.push_back(ch);
          }
        txt = tr.persistent();

        state.files[state.active_file].content = f.content.insert((uint32_t)f.dot.r.p1, txt);
        state.files[state.active_file].dot.r.p2 = state.files[state.active_file].dot.r.p1 + wtext.length();
        state.files[state.active_file].modification_mask |= 1;
        push_undo(ss);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_m& cmd)
        {
        file f = state.files[state.active_file];

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        buffer txt;
        auto tr = txt.transient();
        auto it = f.content.begin() + f.dot.r.p1;
        auto it_end = f.content.begin() + f.dot.r.p2;
        for (; it != it_end; ++it)
          {
          tr.push_back(*it);
          }
        txt = tr.persistent();

        address addr = interpret_address_range(cmd.addr, f);

        if (addr.r.p2 >= f.dot.r.p2)
          {
          f.content = f.content.insert((uint32_t)addr.r.p2, txt);
          f.content = f.content.erase((uint32_t)f.dot.r.p1, (uint32_t)f.dot.r.p2);
          int64_t diff = f.dot.r.p2 - f.dot.r.p1;
          state.files[state.active_file].dot.r.p1 = addr.r.p2 - diff;
          state.files[state.active_file].dot.r.p2 = addr.r.p2 - diff + txt.size();
          }
        else
          {
          f.content = f.content.erase((uint32_t)f.dot.r.p1, (uint32_t)f.dot.r.p2);
          f.content = f.content.insert((uint32_t)addr.r.p2, txt);
          state.files[state.active_file].dot.r.p1 = addr.r.p2;
          state.files[state.active_file].dot.r.p2 = addr.r.p2 + txt.size();
          }

        state.files[state.active_file].content = f.content;
        state.files[state.active_file].modification_mask |= 1;
        push_undo(ss);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_p_dot&)
        {
        auto l1 = get_line_number(state.files[state.active_file].dot.r.p1, state.files[state.active_file].content);
        auto l2 = get_line_number(state.files[state.active_file].dot.r.p2, state.files[state.active_file].content);
        if (gp_jamlib_output)
          *gp_jamlib_output << l1 << L" " << l2 << L" " << state.files[state.active_file].dot.r.p1 << L" " << state.files[state.active_file].dot.r.p2 << std::endl;
        return state;
        }

      std::optional<app_state> operator() (const Cmd_p&)
        {
        const file& current_file = state.files[state.active_file];
        std::wstringstream str;
        auto it = current_file.content.begin() + current_file.dot.r.p1;
        auto it_end = current_file.content.begin() + current_file.dot.r.p2;
        for (; it != it_end; ++it)
          str << *it;
        if (gp_jamlib_output)
          *gp_jamlib_output << str.str() << L"\n";
        return state;
        }

      std::optional<app_state> operator() (const Cmd_q&)
        {
        return std::nullopt;
        }

      std::optional<app_state> operator() (const Cmd_R& cmd)
        {
        for (uint64_t i = 0; i < cmd.value; ++i)
          {
          if (state.files[state.active_file].undo_redo_index + 1 < state.files[state.active_file].history.size())
            {
            ++state.files[state.active_file].undo_redo_index;
            snapshot ss = state.files[state.active_file].history[(uint32_t)state.files[state.active_file].undo_redo_index];
            state.files[state.active_file].content = ss.content;
            state.files[state.active_file].dot = ss.dot;
            state.files[state.active_file].modification_mask = ss.modification_mask;
            state.files[state.active_file].enc = ss.enc;
            state.files[state.active_file].history = state.files[state.active_file].history.push_back(ss);
            }
          }
        return state;
        }

      std::optional<app_state> operator() (const Cmd_s& cmd)
        {
        try
          {
          file f = state.files[state.active_file];

          //std::regex reg(cmd.regexp.regexp, std::regex::egrep);
          //std::regex reg(cmd.regexp.regexp);
          std::basic_regex<wchar_t> reg(convert_string_to_wstring(cmd.regexp.regexp, f.enc));
          std::match_results<immutable::vector_iterator<wchar_t, false, 5>> reg_match;
          auto it = f.content.begin() + f.dot.r.p1;
          auto it_end = f.content.begin() + f.dot.r.p2;
          if (std::regex_search(it, it_end, reg_match, reg))
            {
            snapshot ss;
            ss.content = f.content;
            ss.dot = f.dot;
            ss.modification_mask = f.modification_mask;
            ss.enc = f.enc;

            auto p1 = std::distance(f.content.begin(), reg_match[0].first);
            auto p2 = std::distance(f.content.begin(), reg_match[0].second);

            buffer txt;
            auto tr = txt.transient();
            auto wtext = convert_string_to_wstring(cmd.txt.text, f.enc);
            for (auto ch : wtext)
              {
              tr.push_back(ch);
              }
            txt = tr.persistent();

            f.content = f.content.erase((uint32_t)p1, (uint32_t)p2);

            state.files[state.active_file].content = f.content.insert((uint32_t)p1, txt);
            state.files[state.active_file].dot.r.p1 = p1;
            state.files[state.active_file].dot.r.p2 = p1 + wtext.length();
            state.files[state.active_file].modification_mask |= 1;
            push_undo(ss);
            }
          }
        catch (std::regex_error e)
          {
          throw_error(invalid_regex, e.what());
          }
        return state;
        }

      std::optional<app_state> operator() (const Cmd_t& cmd)
        {
        file f = state.files[state.active_file];

        snapshot ss;
        ss.content = f.content;
        ss.dot = f.dot;
        ss.modification_mask = f.modification_mask;
        ss.enc = f.enc;

        buffer txt;
        auto tr = txt.transient();
        auto it = f.content.begin() + f.dot.r.p1;
        auto it_end = f.content.begin() + f.dot.r.p2;
        for (; it != it_end; ++it)
          {
          tr.push_back(*it);
          }
        txt = tr.persistent();

        address addr = interpret_address_range(cmd.addr, f);

        state.files[state.active_file].content = f.content.insert((uint32_t)addr.r.p2, txt);
        state.files[state.active_file].dot.r.p1 = addr.r.p2;
        state.files[state.active_file].dot.r.p2 = state.files[state.active_file].dot.r.p1 + txt.size();
        state.files[state.active_file].modification_mask |= 1;
        push_undo(ss);

        return state;
        }

      std::optional<app_state> operator() (const Cmd_u& cmd)
        {
        if (state.files[state.active_file].undo_redo_index == state.files[state.active_file].history.size()) // if first time undo
          {
          snapshot ss;
          ss.content = state.files[state.active_file].content;
          ss.dot = state.files[state.active_file].dot;
          ss.modification_mask = state.files[state.active_file].modification_mask;
          ss.enc = state.files[state.active_file].enc;
          push_undo(ss); // save last state on undo stack so that we can redo it later
          --state.files[state.active_file].undo_redo_index;
          }
        for (uint64_t i = 0; i < cmd.value; ++i)
          {
          if (state.files[state.active_file].undo_redo_index)
            {
            --state.files[state.active_file].undo_redo_index;
            snapshot ss = state.files[state.active_file].history[(uint32_t)state.files[state.active_file].undo_redo_index];
            state.files[state.active_file].content = ss.content;
            state.files[state.active_file].dot = ss.dot;
            state.files[state.active_file].modification_mask = ss.modification_mask;
            state.files[state.active_file].enc = ss.enc;
            state.files[state.active_file].history = state.files[state.active_file].history.push_back(ss);
            }
          }
        return state;
        }

      std::optional<app_state> operator() (const Cmd_v& cmd)
        {
        try
          {
          file f = state.files[state.active_file];

          snapshot ss;
          ss.content = f.content;
          ss.dot = f.dot;
          ss.modification_mask = f.modification_mask;
          ss.enc = f.enc;

          //std::regex reg(cmd.regexp.regexp, std::regex::egrep);
          //std::regex reg(cmd.regexp.regexp);
          std::basic_regex<wchar_t> reg(convert_string_to_wstring(cmd.regexp.regexp, f.enc));

          bool save_undo_backup = save_undo;

          save_undo = false;

          auto it = state.files[state.active_file].content.begin() + f.dot.r.p1;
          auto it_end = state.files[state.active_file].content.begin() + f.dot.r.p2;

          std::match_results<immutable::vector_iterator<wchar_t, false, 5>> reg_match;
          if (!std::regex_search(it, it_end, reg_match, reg))
            {
            auto new_state = std::visit(*this, cmd.cmd.front());
            if (!new_state)
              return new_state;
            state = *new_state;
            }

          save_undo = save_undo_backup;

          push_undo(ss);
          }
        catch (std::regex_error e)
          {
          throw_error(invalid_regex, e.what());
          }

        return state;
        }

      std::optional<app_state> operator() (const Cmd_x& cmd)
        {
        try
          {
          file f = state.files[state.active_file];

          snapshot ss;
          ss.content = f.content;
          ss.dot = f.dot;
          ss.modification_mask = f.modification_mask;
          ss.enc = f.enc;

          //std::regex reg(cmd.regexp.regexp, std::regex::egrep);
          //std::regex reg(cmd.regexp.regexp);
          std::basic_regex<wchar_t> reg(convert_string_to_wstring(cmd.regexp.regexp, f.enc));

          bool found = true;

          int64_t iterator_pos = f.dot.r.p1;
          int64_t iterator_distance_to_end = (int64_t)f.content.size() - f.dot.r.p2;

          bool save_undo_backup = save_undo;

          save_undo = false;

          while (found)
            {
            if (iterator_distance_to_end > state.files[state.active_file].content.size())
              break;
            found = false;
            auto it = state.files[state.active_file].content.begin() + iterator_pos;
            auto it_end = state.files[state.active_file].content.end() - iterator_distance_to_end;
            if (it >= it_end)
              break;
            std::match_results<immutable::vector_iterator<wchar_t, false, 5>> reg_match;
            if (std::regex_search(it, it_end, reg_match, reg))
              {
              state.files[state.active_file].dot.r.p1 = std::distance(state.files[state.active_file].content.begin(), reg_match[0].first);
              state.files[state.active_file].dot.r.p2 = std::distance(state.files[state.active_file].content.begin(), reg_match[0].second);

              int64_t offset = 0;
              if (state.files[state.active_file].dot.r.p1 == state.files[state.active_file].dot.r.p2)
                offset = 1;

              //int64_t local_distance_to_end = (int64_t)state.files[state.active_file].content.size() - state.files[state.active_file].dot.r.p2;

              auto new_state = std::visit(*this, cmd.cmd.front());
              if (!new_state)
                return new_state;
              state = *new_state;

              iterator_pos = state.files[state.active_file].dot.r.p2 + offset;
              //if (iterator_pos < (int64_t)state.files[state.active_file].content.size() - local_distance_to_end + offset)
              //  iterator_pos = (int64_t)state.files[state.active_file].content.size() - local_distance_to_end + offset;

              found = true;
              }
            }

          save_undo = save_undo_backup;

          push_undo(ss);
          }
        catch (std::regex_error e)
          {
          throw_error(invalid_regex, e.what());
          }
        return state;
        }

      std::optional<app_state> operator() (const Cmd_e& cmd)
        {
        if (file_exists(cmd.filename))
          {
          buffer new_b = read_buffer_from_file(cmd.filename, state.files[state.active_file].enc);

          snapshot ss;
          ss.content = state.files[state.active_file].content;
          ss.dot = state.files[state.active_file].dot;
          ss.modification_mask = state.files[state.active_file].modification_mask;
          ss.enc = state.files[state.active_file].enc;

          state.files[state.active_file].content = new_b;
          state.files[state.active_file].dot.r.p1 = 0;
          state.files[state.active_file].dot.r.p2 = new_b.size();
          state.files[state.active_file].modification_mask |= 1;
          push_undo(ss);
          }
        return state;
        }

      std::optional<app_state> operator() (const Cmd_l& cmd)
        {
        if (cmd.filename.empty())
          {
          const uint64_t file_id = state.files.size();
          state.files.push_back(make_empty_file(file_id));
          state.active_file = file_id;
          }
        else if (file_exists(cmd.filename))
          {
          const uint64_t file_id = state.files.size();
          state.files.push_back(read_file(cmd.filename, file_id));
          state.active_file = file_id;
          }
        return state;
        }

      std::optional<app_state> operator() (const Cmd_r& cmd)
        {
        if (file_exists(cmd.filename))
          {
          buffer new_b = read_buffer_from_file(cmd.filename, state.files[state.active_file].enc);

          snapshot ss;
          ss.content = state.files[state.active_file].content;
          ss.dot = state.files[state.active_file].dot;
          ss.modification_mask = state.files[state.active_file].modification_mask;
          ss.enc = state.files[state.active_file].enc;

          state.files[state.active_file].content = state.files[state.active_file].content.erase((uint32_t)state.files[state.active_file].dot.r.p1, (uint32_t)state.files[state.active_file].dot.r.p2);

          state.files[state.active_file].content = state.files[state.active_file].content.insert((uint32_t)state.files[state.active_file].dot.r.p1, new_b);
          state.files[state.active_file].dot.r.p2 = state.files[state.active_file].dot.r.p1 + new_b.size();
          state.files[state.active_file].modification_mask |= 1;
          push_undo(ss);
          }
        return state;
        }

      std::optional<app_state> operator() (const Cmd_w& cmd)
        {
        #ifdef _WIN32
        std::wstring wfilename = convert_string_to_wstring(cmd.filename, ENC_UTF8); // filenames are in utf8 encoding
        #else
        std::string wfilename(cmd.filename);
        #endif
        auto f = std::ofstream{ wfilename };
        if (f.is_open())
          {
          file& current_file = state.files[state.active_file];
          std::string str;
          auto it = current_file.content.begin();
          auto it_end = current_file.content.end();
          str.reserve(std::distance(it, it_end));
          switch (current_file.enc)
            {
            case ENC_ASCII:
              for (; it != it_end; ++it)
                str.push_back((char)*it);
            case ENC_UTF8:
            {
            utf8::utf16to8(it, it_end, std::back_inserter(str));
            break;
            }
            }
          f << str;
          f.close();
          current_file.modification_mask = 0;
          auto thistory = current_file.history.transient();
          for (uint32_t idx = 0; idx < thistory.size(); ++idx)
            {
            auto h = thistory[idx];
            h.modification_mask = 1;
            thistory.set(idx, h);
            }
          current_file.history = thistory.persistent();
          }
        return state;
        }

      std::optional<app_state> operator() (const Cmd_null&)
        {
        assert(0);
        return state;
        }

      };

    struct simple_address_handler
      {
      file f;
      int64_t starting_pos;
      bool reverse;

      simple_address_handler(file i_f, int64_t i_starting_pos, bool i_reverse) : f(i_f),
        starting_pos(i_starting_pos), reverse(i_reverse) {}

      void check_range(range& r)
        {
        if (r.p2 < r.p1)
          std::swap(r.p1, r.p2);
        if (r.p1 < 0)
          r.p1 = 0;
        if (r.p2 < 0)
          r.p2 = 0;
        if (r.p1 > f.content.size())
          r.p1 = f.content.size();
        if (r.p2 > f.content.size())
          r.p2 = f.content.size();
        }

      range operator()(const CharacterNumber& cn)
        {
        range r;
        if (reverse)
          {
          r.p1 = r.p2 = starting_pos - cn.value;
          }
        else
          {
          r.p1 = r.p2 = cn.value + starting_pos;
          }
        check_range(r);
        return r;
        }

      range operator()(const Dot&)
        {
        range r = f.dot.r;
        if (reverse)
          {
          r.p1 = starting_pos - f.dot.r.p1;
          r.p2 = starting_pos - f.dot.r.p2;
          }
        else
          {
          r.p1 += starting_pos;
          r.p2 += starting_pos;
          }
        check_range(r);
        return r;
        }

      range operator()(const EndOfFile&)
        {
        range r;
        r.p1 = r.p2 = f.content.size();
        return r;
        }

      range operator()(const LineNumber& ln)
        {
        if (starting_pos)
          {
          if (reverse)
            {
            starting_pos = find_previous_end_of_line_position(starting_pos, f);
            }
          else
            {
            //starting_pos = find_next_line_position(starting_pos, f);
            }
          }
        if (ln.value == 0)
          {
          range r;
          r.p1 = r.p2 = starting_pos;
          return r;
          }
        if (reverse)
          {
          range r;
          r.p1 = 0;
          r.p2 = starting_pos;
          uint64_t current_line = 1;
          int64_t position = starting_pos;
          auto it = f.content.rbegin() + (f.content.size() - starting_pos);
          auto it_end = f.content.rend();
          for (; it != it_end; ++it, --position)
            {
            if (*it == '\n')
              {
              ++current_line;
              if (current_line == ln.value)
                r.p2 = position - 1;
              if (current_line > ln.value)
                {
                r.p1 = position;
                return r;
                }
              }
            }
          return r;
          }
        else
          {
          range r;
          r.p1 = starting_pos;
          r.p2 = f.content.size();
          uint64_t current_line = 1;
          int64_t position = r.p1;
          auto it = f.content.begin() + r.p1;
          auto it_end = f.content.end();
          for (; it != it_end; ++it, ++position)
            {
            if (*it == '\n')
              {
              ++current_line;
              if (current_line == ln.value)
                r.p1 = position + 1;
              if (current_line > ln.value)
                {
                r.p2 = position + 1;
                return r;
                }
              }
            }
          return r;
          }
        }

      range operator()(const RegExp& re)
        {
        range ret;
        ret.p1 = ret.p2 = 0;
        try
          {
          ret = find_regex_range(re.regexp, f.content, reverse, starting_pos, f.enc);
          }
        catch (std::regex_error e)
          {
          throw_error(invalid_regex, e.what());
          }
        return ret;
        }

      };

    address interpret_simple_address(const SimpleAddress& term, int64_t starting_pos, bool reverse, file f)
      {
      address out;
      out.file_id = f.file_id;
      simple_address_handler sah(f, starting_pos, reverse);
      out.r = std::visit(sah, term);
      return out;
      }

    address interpret_address_term(const AddressTerm& addr, file f)
      {
      address out;
      out.file_id = f.file_id;
      if (addr.operands.size() > 2)
        throw_error(invalid_address);
      if (addr.operands.empty())
        throw_error(invalid_address);
      out = interpret_simple_address(addr.operands[0], 0, false, f);
      if (addr.operands.size() == 2)
        {
        if (addr.fops[0] == "+")
          {
          return interpret_simple_address(addr.operands[1], out.r.p2, false, f);
          }
        else if (addr.fops[0] == "-")
          {
          return interpret_simple_address(addr.operands[1], out.r.p1, true, f);
          }
        else
          throw_error(not_implemented, addr.fops[0]);
        }
      return out;
      }

    address interpret_address_range(const AddressRange& addr, file f)
      {
      address out;
      out.file_id = f.file_id;
      if (addr.operands.size() > 2)
        throw_error(invalid_address);
      if (addr.operands.empty())
        throw_error(invalid_address);
      out = interpret_address_term(addr.operands[0], f);
      if (addr.operands.size() == 2)
        {
        address right = interpret_address_term(addr.operands[1], f);
        if (addr.fops[0] == ",")
          {
          out.r.p2 = right.r.p2;
          }
        else
          throw_error(not_implemented, addr.fops[0]);
        }
      return out;
      }

    struct expression_handler
      {
      app_state state;

      expression_handler(app_state i_state) : state(i_state) {}

      std::optional<app_state> operator() (const AddressRange& addr)
        {
        state.files[state.active_file].dot = interpret_address_range(addr, state.files[state.active_file]);
        return state;
        }

      std::optional<app_state> operator() (const Command& cmd)
        {
        command_handler ch(state);
        return std::visit(ch, cmd);
        }
      };

    }

  std::optional<app_state> handle_command(app_state state, std::string command)
    {
    auto tokens = tokenize(command);
    auto cmds = parse(tokens);
    for (const auto& cmd : cmds)
      {
      expression_handler eh(state);
      if (std::optional<app_state> new_state = std::visit(eh, cmd))
        state = *new_state;
      else
        return std::nullopt;
      }

    return state;
    }

  app_state init_state(int argc, char** argv)
    {
    app_state state;
    for (int j = 1; j < argc; ++j)
      {
      const uint64_t file_id = state.files.size();
      state.files.push_back(read_file(argv[j], file_id));
      }
    if (state.files.empty())
      state.files.push_back(make_empty_file(0));
    state.active_file = 0;
    return state;
    }

  void parse_command(std::string& executable_name, std::string& folder, std::string& parameters, std::string command)
    {
    executable_name.clear();
    folder.clear();
    parameters.clear();
    auto pos = command.find_first_not_of(' ');
    command = command.substr(pos);
    if (command.empty())
      return;
    std::string path;
    if (command.front() == '"')
      {
      path = command.substr(1);
      pos = path.find_first_of('"');
      if (pos != std::string::npos)
        parameters = path.substr(pos + 1);
      path = path.substr(0, pos);
      }
    else
      {
      pos = command.find_first_of(' ');
      if (pos != std::string::npos)
        parameters = command.substr(pos + 1);
      path = command.substr(0, pos);
      }
    executable_name = get_filename(path);
    folder = get_folder(path);
    }

  void set_output_stream(std::wostream* output)
    {
    if (output)
      gp_jamlib_output = output;
    else
      gp_jamlib_output = &std::wcout;
    }
  }