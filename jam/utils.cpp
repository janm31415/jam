#include "utils.h"
#include "file_utils.h"
#include <windows.h>

#include <encoding.h>


bool is_modified(const jamlib::file& f)
  {
  if (f.filename.empty())
    return false;
  if (f.filename.front() == '+')
    return false;
  if (is_directory(f.filename))
    return false;
  return f.modification_mask != 0;
  }

bool ask_user_to_save_modified_file(const jamlib::file& f)
  {
  if (f.filename.empty())
    return false;
  if (f.filename.front() == '+')
    return false;
  if (is_directory(f.filename))
    return false;
  return (f.modification_mask & 1) == 1;
  }

bool should_be_cleaned(char ch)
  {
  switch (ch)
    {
    case '\n': return true;
    case ' ': return true;
    case '\t': return true;
    }
  return false;
  }

std::string cleanup(std::string cmd)
  {
  while (!cmd.empty() && should_be_cleaned(cmd.back()))
    cmd.pop_back();
  while (!cmd.empty() && should_be_cleaned(cmd.front()))
    cmd.erase(cmd.begin());
  return cmd;
  }

std::string resolve_regex_escape_characters(const std::string& cmd)
  {
  std::string out;
  for (auto ch : cmd)
    {
    switch (ch)
      {
      case '{':
      case '}':
      case '.':
      case ',':
      case '^':
      case '$':
      case '+':
      case '-':
      case '*':
      case '|':
      case '?':
      case '\\':
      case '/':
      case '(':
      case ')': out.push_back('\\'); out.push_back(ch); break;
      default: out.push_back(ch);
      }

    }
  return out;
  }

std::string resolve_jamlib_escape_characters(const std::string& cmd)
  {
  std::string out;
  for (auto ch : cmd)
    {
    if (ch == '\\')
      {
      out.push_back('\\');
      out.push_back('\\');
      }
    else if (ch == '/')
      {
      out.push_back('\\');
      out.push_back('/');
      }
    else if (ch == '\n')
      {
      out.push_back('\\');
      out.push_back('n');
      }
    else
      out.push_back(ch);
    }
  return out;
  }

std::string remove_quotes_from_path(std::string str)
  {
  str = cleanup(str);
  if (str.size() < 2)
    return str;
  if (str.front() == '"' && str.back() == '"')
    return str.substr(1, str.length() - 2);
  return str;
  }

std::string get_file_in_executable_path(const std::string& filename)
  {
  auto folder = get_folder(get_executable_path());
  return folder + filename;
  }

std::string get_file_path(const std::string& filename, const std::string& window_filename)
  {
  if (!get_folder(filename).empty())
    {
    if (file_exists(filename))
      return filename;
    }
  if (!window_filename.empty())
    {
    auto window_folder = get_folder(window_filename);
    auto possible_executables = get_files_from_directory(window_folder, false);
    for (const auto& path : possible_executables)
      {
      auto f = get_filename(path);
      if (f == filename || remove_extension(f) == filename)
        {
        return path;
        }
      }
    }
  auto executable_path = get_folder(get_executable_path());
  auto possible_executables = get_files_from_directory(executable_path, false);
  for (const auto& path : possible_executables)
    {
    auto f = get_filename(path);
    if (f == filename || remove_extension(f) == filename)
      {
      return path;
      }
    }
  wchar_t buf[MAX_PATH];
  GetCurrentDirectoryW(MAX_PATH, buf);
  std::string dir = JAM::convert_wstring_to_string(std::wstring(buf));
  possible_executables = get_files_from_directory(dir, false);
  for (const auto& path : possible_executables)
    {
    auto f = get_filename(path);
    if (f == filename || remove_extension(f) == filename)
      {
      return path;
      }
    }
  return "";
  }

std::string flip_backslash_to_slash_in_filename(const std::string& filename)
  {
  std::wstring wfilename = JAM::convert_string_to_wstring(filename);
  std::replace(wfilename.begin(), wfilename.end(), '\\', '/'); // replace all '\\' to '/'
  return JAM::convert_wstring_to_string(wfilename);
  }