#pragma once

#include "namespace.h"
#include "encoding.h"
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else

#endif

JAM_BEGIN

#ifdef _WIN32

inline std::string get_executable_path()
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
    return JAM::convert_wstring_to_string(wret);
  }

#else

inline std::string get_executable_path()
  {
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  return std::string(result, (count > 0) ? count : 0);
  }

#endif

JAM_END