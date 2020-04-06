#include "file_utils.h"

#include <fstream>
#include <algorithm>
#include <cctype>

#pragma warning (push)
#pragma warning(disable: 4505)

#include "dirent.h"

#include <jam_utf8.h>

#include <jamlib/encoding.h>

std::wstring get_wide(const std::string& filename)
  {
  std::wstring wfilename = jamlib::convert_string_to_wstring(filename, jamlib::ENC_UTF8);
  return wfilename;
  }

std::string get_utf8(const std::wstring& txt)
  {
  return jamlib::convert_wstring_to_string(txt, jamlib::ENC_UTF8);
  }

std::string get_executable_path()
  {
  typedef std::vector<wchar_t> char_vector;
  typedef std::vector<wchar_t>::size_type size_type;
  char_vector buf(1024, 0);
  size_type size = buf.size();
  bool havePath = false;
  bool shouldContinue = true;
  do
    {
    DWORD result = GetModuleFileNameW(nullptr, &buf[0], (DWORD)size);
    DWORD lastError = GetLastError();
    if (result == 0)
      {
      shouldContinue = false;
      }
    else if (result < size)
      {
      havePath = true;
      shouldContinue = false;
      }
    else if (
      result == size
      && (lastError == ERROR_INSUFFICIENT_BUFFER || lastError == ERROR_SUCCESS)
      )
      {
      size *= 2;
      buf.resize(size);
      }
    else
      {
      shouldContinue = false;
      }
    } while (shouldContinue);
    if (!havePath)
      {
      return std::string("");
      }
    std::wstring wret = &buf[0];
    return jamlib::convert_wstring_to_string(wret, jamlib::ENC_UTF8);
  }

bool file_exists(const std::string& filename)
  {
  std::wstring wfilename = get_wide(filename);
  std::ifstream f;
  f.open(wfilename, std::ifstream::in);
  if (f.fail())
    return false;
  f.close();
  return true;
  }

std::string get_extension(const std::string& filename)
  {
  std::wstring wfilename = get_wide(filename);
  auto ext_ind = wfilename.find_last_of('.');
  std::wstring ext;
  if (ext_ind != std::wstring::npos)
    ext = wfilename.substr(ext_ind + 1);
  return get_utf8(ext);
  }

std::string remove_extension(const std::string& filename)
  {
  std::wstring wfilename = get_wide(filename);
  auto ext_ind = wfilename.find_last_of('.');
  if (ext_ind == std::wstring::npos)
    return filename;
  return get_utf8(wfilename.substr(0, ext_ind));
  }

std::string get_folder(const std::string& path)
  {
  std::wstring wpath = get_wide(path);
  auto pos1 = wpath.find_last_of('/');
  auto pos2 = wpath.find_last_of('\\');
  if (pos1 == std::wstring::npos && pos2 == std::wstring::npos)
    return ".";
  if (pos1 == std::wstring::npos)
    return get_utf8(wpath.substr(0, pos2 + 1));
  if (pos2 == std::wstring::npos)
    return get_utf8(wpath.substr(0, pos1 + 1));
  return get_utf8(wpath.substr(0, std::max<std::size_t>(pos1, pos2) + 1));
  }

std::string get_filename(const std::string& path)
  {
  std::wstring wpath = get_wide(path);
  auto pos1 = wpath.find_last_of('/');
  auto pos2 = wpath.find_last_of('\\');
  if (pos1 == std::wstring::npos && pos2 == std::wstring::npos)
    return path;
  if (pos1 == std::wstring::npos)
    return get_utf8(wpath.substr(pos2 + 1));
  if (pos2 == std::wstring::npos)
    return get_utf8(wpath.substr(pos1 + 1));
  return get_utf8(wpath.substr(std::max<std::size_t>(pos1, pos2) + 1));
  }

struct dir_iterator
  {
  dir_iterator() : dir(nullptr), ent(nullptr) {}
  _WDIR* dir;
  _wdirent* ent;
  };

directory_iterator open_directory(const std::string& directory)
  {
  std::wstring wdirectory = get_wide(directory);
  dir_iterator* iter = new dir_iterator;
  if (directory.length() >= PATH_MAX)
    iter->dir = nullptr;
  else
    iter->dir = wopendir(wdirectory.c_str());
  if (iter->dir != nullptr)
    {
    iter->ent = wreaddir(iter->dir);
    }
  return iter;
  }

bool is_valid(directory_iterator iter)
  {
  if (iter->dir)
    return (iter->ent != nullptr);
  return false;
  }

std::string get_filename(directory_iterator iter)
  {
  std::wstring wname(iter->ent->d_name);
  return get_utf8(wname);
  }

void next_filename(directory_iterator iter)
  {
  if (iter->dir != nullptr)
    {
    iter->ent = wreaddir(iter->dir);
    }
  }

void close_directory(directory_iterator& iter)
  {
  if (iter->dir != nullptr)
    {
    wclosedir(iter->dir);
    }
  delete iter;
  iter = nullptr;
  }

bool is_directory(directory_iterator iter)
  {
  return iter->ent->d_type == DT_DIR;
  }

bool is_file(directory_iterator iter)
  {
  return iter->ent->d_type == DT_REG;
  }

bool is_directory(const std::string& directory)
  {
  auto iter = open_directory(directory);
  if (is_valid(iter))
    {
    close_directory(iter);
    return true;
    }
  return false;
  }

std::vector<std::string> get_files_from_directory(const std::string& d, bool subfolders)
  {
  std::string dir(d);
  if (!dir.empty() && !(dir.back() == '/' || dir.back() == '\\'))
    dir.push_back('/');
  std::vector<std::string> files;
  auto iter = open_directory(dir);
  while (is_valid(iter))
    {
    if (is_file(iter))
      files.push_back(dir + get_filename(iter));
    else if (subfolders && is_directory(iter))
      {
      std::string n = get_filename(iter);
      if (n.front() != '.')
        {
        std::string path = dir + n;
        auto files_sub = get_files_from_directory(path, subfolders);
        files.insert(files.end(), files_sub.begin(), files_sub.end());
        }
      }
    next_filename(iter);
    }
  close_directory(iter);
  return files;
  }

std::vector<std::string> get_subdirectories_from_directory(const std::string& dir, bool subfolders)
  {
  std::vector<std::string> dirs;
  auto iter = open_directory(dir);
  while (is_valid(iter))
    {
    if (is_directory(iter))
      {
      std::string n = get_filename(iter);
      if (n.front() != '.')
        {
        dirs.push_back(dir + "/" + n);
        if (subfolders)
          {
          auto dirs_sub = get_subdirectories_from_directory(dirs.back(), subfolders);
          dirs.insert(dirs.end(), dirs_sub.begin(), dirs_sub.end());
          }
        }
      }
    next_filename(iter);
    }
  close_directory(iter);
  return dirs;
  }

std::vector<std::string> get_list_from_directory(const std::string& dir, bool subfolders)
  {
  std::vector<std::string> files;
  auto iter = open_directory(dir);
  while (is_valid(iter))
    {
    if (is_file(iter))
      files.push_back(dir + "/" + get_filename(iter));
    else if (is_directory(iter))
      {
      std::string n = get_filename(iter);
      if (n.front() != '.')
        {
        files.push_back(dir + "/" + n);
        if (subfolders)
          {
          auto files_sub = get_list_from_directory(files.back(), subfolders);
          files.insert(files.end(), files_sub.begin(), files_sub.end());
          }
        }
      }
    next_filename(iter);
    }
  close_directory(iter);
  return files;
  }

#pragma warning (pop)