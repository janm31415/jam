#pragma once

#include "jam_namespace.h"
#include "jam_encoding.h"

#ifdef _WIN32
#include "jam_dirent.h"
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#if defined(unix) || defined(__unix) || defined(__unix__) || defined(UNIX)
#include <linux/limits.h>
#endif
#endif

#include <string>
#include <vector>

JAM_BEGIN

inline bool is_directory(const std::string& directory)
  {
#ifdef _WIN32
  std::wstring wdirectory = convert_string_to_wstring(directory);
  if (wdirectory.length() >= PATH_MAX)
    return false;
  bool result = false;
  _WDIR* dir = wopendir(wdirectory.c_str());
  if (dir)
    {
    _wdirent* ent = wreaddir(dir);
    if (ent)
      result = true;
    wclosedir(dir);
    }
  return result;
#else
  if (directory.length() >= PATH_MAX)
    return false;
  bool result = false;
  DIR* dir = opendir(directory.c_str());
  if (dir)
    {
    dirent* ent = readdir(dir);
    if (ent)
      result = true;
    closedir(dir);
    }
  return result;
#endif  
  }

inline bool file_exists(const std::string& filename)
  {
#ifdef _WIN32
  std::wstring wfilename = convert_string_to_wstring(filename);
  std::ifstream f;
  f.open(wfilename, std::ifstream::in);
  if (f.fail())
    return false;
  f.close();
  return true;
#else
  if (is_directory(filename)) // This should not be necessary, but otherwise crash with gcc on Ubuntu
    return false;
  struct stat buffer;
  return (stat(filename.c_str(), &buffer) == 0);
#endif
  }  

inline std::vector<std::string> get_files_from_directory(const std::string& d, bool include_subfolders)
  {
  std::string directory(d);
  if (!directory.empty() && !(directory.back() == '/' || directory.back() == '\\'))
    directory.push_back('/');
  std::vector<std::string> files;
#ifdef _WIN32
  std::wstring wdirectory = convert_string_to_wstring(directory);
  _WDIR* dir = wopendir(wdirectory.c_str());
  _wdirent* ent = nullptr;
  if (dir)
    ent = wreaddir(dir);
#else
  DIR* dir = opendir(directory.c_str());
  dirent* ent = nullptr;
  if (dir)
    ent = readdir(dir);
#endif
  if (!dir)
    return files;
  while (ent)
    {
    if (ent->d_type == DT_REG) // a file
      {
#ifdef _WIN32
      files.push_back(directory + convert_wstring_to_string(std::wstring(ent->d_name)));
#else
      files.push_back(directory + std::string(ent->d_name));
#endif
      }
    else if (include_subfolders && ent->d_type == DT_DIR) // a directory
      {
#ifdef _WIN32
      std::string n = convert_wstring_to_string(std::wstring(ent->d_name));
#else
      std::string n(ent->d_name);
#endif
      if (n.front() != '.')
        {
        std::string path = directory + n;
        auto files_sub = get_files_from_directory(path, include_subfolders);
        files.insert(files.end(), files_sub.begin(), files_sub.end());
        }
      }

#ifdef _WIN32
    ent = wreaddir(dir);
#else
    ent = readdir(dir);
#endif
    }
#ifdef _WIN32
  wclosedir(dir);
#else
  closedir(dir);
#endif
  return files;
  }

inline std::vector<std::string> get_subdirectories_from_directory(const std::string& d, bool include_subfolders)
  {
  std::string directory(d);
  if (!directory.empty() && !(directory.back() == '/' || directory.back() == '\\'))
    directory.push_back('/');
  std::vector<std::string> files;
#ifdef _WIN32
  std::wstring wdirectory = convert_string_to_wstring(directory);
  _WDIR* dir = wopendir(wdirectory.c_str());
  _wdirent* ent = nullptr;
  if (dir)
    ent = wreaddir(dir);
#else
  DIR* dir = opendir(directory.c_str());
  dirent* ent = nullptr;
  if (dir)
    ent = readdir(dir);
#endif
  if (!dir)
    return files;
  while (ent)
    {
    if (ent->d_type == DT_DIR) // a directory
      {
#ifdef _WIN32
      std::string n = convert_wstring_to_string(std::wstring(ent->d_name));
#else
      std::string n(ent->d_name);
#endif
      if (n.front() != '.')
        {
        files.push_back(directory + n);
        if (include_subfolders)
          {
          auto files_sub = get_subdirectories_from_directory(files.back(), include_subfolders);
          files.insert(files.end(), files_sub.begin(), files_sub.end());
          }
        }
      }

#ifdef _WIN32
    ent = wreaddir(dir);
#else
    ent = readdir(dir);
#endif
  }
#ifdef _WIN32
  wclosedir(dir);
#else
  closedir(dir);
#endif
  return files;
  }

inline std::vector<std::string> get_list_from_directory(const std::string& d, bool include_subfolders)
  {
  std::string directory(d);
  if (!directory.empty() && !(directory.back() == '/' || directory.back() == '\\'))
    directory.push_back('/');
  std::vector<std::string> files;
#ifdef _WIN32
  std::wstring wdirectory = convert_string_to_wstring(directory);
  _WDIR* dir = wopendir(wdirectory.c_str());
  _wdirent* ent = nullptr;
  if (dir)
    ent = wreaddir(dir);
#else
  DIR* dir = opendir(directory.c_str());
  dirent* ent = nullptr;
  if (dir)
    ent = readdir(dir);
#endif
  if (!dir)
    return files;
  while (ent)
    {
    if (ent->d_type == DT_REG) // a file
      {
#ifdef _WIN32
      files.push_back(directory + convert_wstring_to_string(std::wstring(ent->d_name)));
#else
      files.push_back(directory + std::string(ent->d_name));
#endif
    }
    else if (ent->d_type == DT_DIR) // a directory
      {
#ifdef _WIN32
      std::string n = convert_wstring_to_string(std::wstring(ent->d_name));
#else
      std::string n(ent->d_name);
#endif

      if (n.front() != '.')
        {
        files.push_back(directory + n);
        if (include_subfolders)
          {          
          auto files_sub = get_list_from_directory(files.back(), include_subfolders);
          files.insert(files.end(), files_sub.begin(), files_sub.end());
          }
        }
      }

#ifdef _WIN32
    ent = wreaddir(dir);
#else
    ent = readdir(dir);
#endif
  }
#ifdef _WIN32
  wclosedir(dir);
#else
  closedir(dir);
#endif
  return files;
  }

JAM_END
