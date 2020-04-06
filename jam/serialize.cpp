#include "serialize.h"
#include "window.h"
#include "grid.h"

#include <jamlib/encoding.h>

#include <fstream>


void save_to_stream(std::ostream& str, const app_state& state)
  {
  str << state.w << std::endl;
  str << state.h << std::endl;
  str << (uint32_t)state.windows.size() << std::endl;
  for (const auto& w : state.windows)
    save_window_to_stream(str, w);
  str << (uint32_t)state.file_id_to_window_id.size() << std::endl;
  for (auto v : state.file_id_to_window_id)
    str << v << std::endl;
  str << (uint32_t)state.window_pairs.size() << std::endl;
  for (const auto& w : state.window_pairs)
    save_window_pair_to_stream(str, w);
  save_grid_to_stream(str, state.g);
  save_file_state_to_stream(str, state);
  }

app_state load_from_stream(std::istream& str)
  {
  app_state result;
  str >> result.w >> result.w;
  uint32_t sz;
  str >> sz;
  for (uint32_t i = 0; i < sz; ++i)
    {
    result.windows.push_back(load_window_from_stream(str));
    }
  str >> sz;
  for (uint32_t i = 0; i < sz; ++i)
    {
    uint32_t v;
    str >> v;
    result.file_id_to_window_id.push_back(v);
    }
  str >> sz;
  for (uint32_t i = 0; i < sz; ++i)
    {
    result.window_pairs.push_back(load_window_pair_from_stream(str));
    }
  result.g = load_grid_from_stream(str);
  result.file_state = load_file_state_from_stream(str);
  return result;
  }

void save_to_file(const std::string& filename, const app_state& state)
  {
  std::ofstream f(filename);
  if (f.is_open())
    {
    save_to_stream(f, state);
    }
  f.close();
  }

app_state load_from_file(const std::string& filename)
  {
  app_state result;
  std::ifstream f(filename);
  if (f.is_open())
    {
    result = load_from_stream(f);
    f.close();
    }
  return result;
  }

void save_file_state_to_stream(std::ostream& str, const app_state& state)
  {
  str << state.file_state.active_file << std::endl;
  str << (uint32_t)state.file_state.files.size() << std::endl;
  uint32_t id = 0;
  for (const auto& f : state.file_state.files)
    {
    save_file_to_stream(str, f, state.windows[state.file_id_to_window_id[id]].is_command_window);
    ++id;
    }
  }

std::string make_filename(const std::string& f)
  {
  //std::string out;
  //if (f.empty())
  //  return "+empty";
  //if (f.find(' ') != std::string::npos)
  //  {
  //  out.push_back('"');
  //  out.insert(out.end(), f.begin(), f.end());
  //  out.push_back('"');
  //  }
  //else
  //  out = f;
  //return out;
  if (f.empty())
    return "+empty";
  return f;
  }

void save_file_to_stream(std::ostream& str, const jamlib::file& f, bool command)
  {
  std::string filename = make_filename(f.filename);

  str << filename << std::endl;

  str << command << std::endl;
  if (command)
    {
    std::wstring wstr(f.content.begin(), f.content.end());
    std::string s = jamlib::convert_wstring_to_string(wstr, jamlib::ENC_UTF8);
    str << s << std::endl;
    }
  int e = (int)f.enc;
  str << e << std::endl;
  str << f.dot.r.p1 << std::endl;
  str << f.dot.r.p2 << std::endl;
  str << f.file_id << std::endl;
  }

jamlib::file load_file_from_stream(std::istream& str)
  {
  jamlib::file f;
  std::getline(str, f.filename);
  std::getline(str, f.filename);
  if (f.filename == "+empty")
    {
    f.filename.clear();
    }
  bool is_command;
  str >> is_command;
  if (is_command)
    {
    std::string line;
    std::getline(str, line);
    std::getline(str, line);
    std::wstring ws = jamlib::convert_string_to_wstring(line, jamlib::ENC_UTF8);
    auto tr = f.content.transient();
    for (auto wch : ws)
      tr.push_back(wch);
    f.content = tr.persistent();
    }
  int e;
  str >> e;
  f.enc = (jamlib::encoding)e;
  str >> f.dot.r.p1;
  str >> f.dot.r.p2;
  str >> f.file_id;
  f.dot.file_id = f.file_id;
  f.modification_mask = 0;
  f.undo_redo_index = 0;  
  return f;
  }

jamlib::app_state load_file_state_from_stream(std::istream& str)
  {
  jamlib::app_state file_state;
  str >> file_state.active_file;
  uint32_t sz;
  str >> sz;
  for (uint32_t i = 0; i < sz; ++i)
    {
    file_state.files.push_back(load_file_from_stream(str));
    }
  return file_state;
  }