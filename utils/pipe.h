#pragma once
#include "namespace.h"
#include "encoding.h"

#ifdef _WIN32

#include <windows.h>
#include <chrono>
#include <thread>

#else

#include <sstream>
#include <iostream>
#include <vector>
#include <string.h>
#include <stdio.h>

#endif

JAM_BEGIN

/*
create_pipe
cmdLine:     executable name + parameters
dir:         directory where executable resides (or nullptr: will look in current active directory)
current_dir: if not nullptr, will change active directory to this one
pr:          output process pointer
*/
int create_pipe(const char *cmdLine, const char *dir, const char* current_dir, void** pr);

/*
destroy_pipe
signal = 9 : kill immediately
signal = 10: kill after 3s waiting
*/
void destroy_pipe(void* pr, int signal);

//returns 0 if successful
//message should be utf8 string
int send_to_pipe(void* process, const char* message);

//returns utf8 string
std::string read_from_pipe(void* process, int time_out);

std::string read_std_input(int time_out);

#ifdef _WIN32
struct child_proc
  {
  HANDLE hProcess;
  DWORD pid;
  HANDLE hTo;
  HANDLE hFrom;
  };

inline int create_pipe(const char *cmdLine, const char *dir, const char* current_dir, void** pr)
  {
  HANDLE hChildStdinRd, hChildStdinWr, hChildStdoutRd, hChildStdoutWr;
  HANDLE hChildStdinWrDup, hChildStdoutRdDup;
  SECURITY_ATTRIBUTES saAttr;
  BOOL fSuccess;
  PROCESS_INFORMATION piProcInfo;
  STARTUPINFOW siStartInfo;
  child_proc *cp;
  wchar_t buf[MAX_PATH];
  DWORD err;

  *pr = nullptr;

  /* Set the bInheritHandle flag so pipe handles are inherited. */
  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  /*
  * The steps for redirecting child's STDOUT:
  *     1. Create anonymous pipe to be STDOUT for child.
  *     2. Create a noninheritable duplicate of read handle,
  *         and close the inheritable read handle.
  */

  /* Create a pipe for the child's STDOUT. */
  if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0))
    {
    return GetLastError();
    }

  /* Duplicate the read handle to the pipe, so it is not inherited. */
  fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdoutRd,
    GetCurrentProcess(), &hChildStdoutRdDup, 0,
    FALSE,	/* not inherited */
    DUPLICATE_SAME_ACCESS);
  if (!fSuccess)
    {
    return GetLastError();
    }
  CloseHandle(hChildStdoutRd);

  /*
  * The steps for redirecting child's STDIN:
  *     1. Create anonymous pipe to be STDIN for child.
  *     2. Create a noninheritable duplicate of write handle,
  *         and close the inheritable write handle.
  */

  /* Create a pipe for the child's STDIN. */
  if (!CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, 0)) {
    return GetLastError();
    }

  /* Duplicate the write handle to the pipe, so it is not inherited. */
  fSuccess = DuplicateHandle(GetCurrentProcess(), hChildStdinWr,
    GetCurrentProcess(), &hChildStdinWrDup, 0,
    FALSE,	/* not inherited */
    DUPLICATE_SAME_ACCESS);
  if (!fSuccess) {
    return GetLastError();
    }
  CloseHandle(hChildStdinWr);

  /* Arrange to (1) look in dir for the child .exe file, and
  * (2) have dir be the child's working directory.  Interpret
  * dir relative to the directory WinBoard loaded from. */
  GetCurrentDirectoryW(MAX_PATH, buf);

  if (current_dir)
    {
    std::wstring wdir(convert_string_to_wstring(std::string(current_dir)));
    SetCurrentDirectoryW(wdir.c_str());
    }

  std::wstring wcmdLine;
  if (dir)
    {
    wcmdLine = convert_string_to_wstring(std::string(dir));
    wcmdLine.append(convert_string_to_wstring(std::string(cmdLine)));
    }
  else
    wcmdLine = convert_string_to_wstring(std::string(cmdLine));

  /* Now create the child process. */

  siStartInfo.cb = sizeof(STARTUPINFOW);
  siStartInfo.lpReserved = NULL;
  siStartInfo.lpDesktop = NULL;
  siStartInfo.lpTitle = NULL;
  siStartInfo.dwFlags = STARTF_USESTDHANDLES;
  siStartInfo.cbReserved2 = 0;
  siStartInfo.lpReserved2 = NULL;
  siStartInfo.hStdInput = hChildStdinRd;
  siStartInfo.hStdOutput = hChildStdoutWr;
  siStartInfo.hStdError = hChildStdoutWr;

  fSuccess = CreateProcessW(NULL,
    (LPWSTR)(wcmdLine.c_str()),	   /* command line */
    NULL,	   /* process security attributes */
    NULL,	   /* primary thread security attrs */
    TRUE,	   /* handles are inherited */
    DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
    NULL,	   /* use parent's environment */
    NULL,
    &siStartInfo, /* STARTUPINFO pointer */
    &piProcInfo); /* receives PROCESS_INFORMATION */

  err = GetLastError();
  SetCurrentDirectoryW(buf); /* return to prev directory */
  if (!fSuccess) {
    return err;
    }

  SetPriorityClass(piProcInfo.hProcess, 0x00000080);

  /* Close the handles we don't need in the parent */
  CloseHandle(piProcInfo.hThread);
  CloseHandle(hChildStdinRd);
  CloseHandle(hChildStdoutWr);

  /* Prepare return value */
  cp = (child_proc *)calloc(1, sizeof(child_proc));
  cp->hProcess = piProcInfo.hProcess;
  cp->pid = piProcInfo.dwProcessId;
  cp->hFrom = hChildStdoutRdDup;
  cp->hTo = hChildStdinWrDup;

  *pr = (void *)cp;

  return NO_ERROR;
  }

inline void destroy_pipe(void* pr, int signal)
  {
  child_proc *cp;
  int result;

  cp = (child_proc *)pr;
  if (cp == nullptr)
    return;


  /* TerminateProcess is considered harmful, so... */
  CloseHandle(cp->hTo); /* Closing this will give the child an EOF and hopefully kill it */
  if (cp->hFrom) CloseHandle(cp->hFrom);  /* if NULL, InputThread will close it */
                                          /* The following doesn't work because the chess program
                                          doesn't "have the same console" as WinBoard.  Maybe
                                          we could arrange for this even though neither WinBoard
                                          nor the chess program uses a console for stdio? */
                                          /*!!if (signal) GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, cp->pid);*/

                                          /* [AS] Special termination modes for misbehaving programs... */
  if (signal == 9)
    {
    result = TerminateProcess(cp->hProcess, 0);
    }
  else if (signal == 10)
    {
    DWORD dw = WaitForSingleObject(cp->hProcess, 3 * 1000); // Wait 3 seconds at most

    if (dw != WAIT_OBJECT_0)
      {
      result = TerminateProcess(cp->hProcess, 0);
      }
    }

  CloseHandle(cp->hProcess);

  free(cp);
  }

inline int send_to_pipe(void* process, const char* message)
  {
  int count;
  DWORD dOutCount;
  if (process == nullptr)
    return ERROR_INVALID_HANDLE;
  count = (int)strlen(message);
  if (WriteFile(((child_proc *)process)->hTo, message, count, &dOutCount, NULL))
    {
    if (count == (int)dOutCount)
      return NO_ERROR;
    else
      return (int)GetLastError();
    }
  return SOCKET_ERROR;
  }

#define MAX_SIZE 4096

inline std::string read_from_pipe(void* process, int time_out)
  {
  std::string input;

  if (process == nullptr)
    return "";
  child_proc *cp = (child_proc *)process;

  DWORD bytes_left = 0;

  auto tic = std::chrono::steady_clock::now();
  auto toc = std::chrono::steady_clock::now();
  auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(toc - tic).count();

  bool check_at_least_once = true;

  while (time_elapsed < time_out || check_at_least_once)
    {
    check_at_least_once = false;
    if (PeekNamedPipe(cp->hFrom, NULL, 0, NULL, &bytes_left, NULL) && bytes_left)
      {
      check_at_least_once = true;
      bool bytes_left_to_read = bytes_left > 0;

      char line[MAX_SIZE];

      while (bytes_left_to_read)
        {
        int n = bytes_left;
        if (n >= MAX_SIZE)
          n = MAX_SIZE - 1;
        memset(line, 0, MAX_SIZE);
        DWORD count;
        if (!ReadFile(cp->hFrom, line, n, &count, nullptr))
          return input;
        std::string str(line);
        input.append(str);

        bytes_left -= (DWORD)str.size();
        bytes_left_to_read = bytes_left > 0;
        }
      }
    std::this_thread::sleep_for(std::chrono::milliseconds(time_out / 2 > 10 ? 10 : time_out / 2));
    toc = std::chrono::steady_clock::now();
    time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(toc - tic).count();
    }
  return input;
  }

inline std::string read_std_input(int time_out)
  {
  child_proc pr;
  pr.hFrom = GetStdHandle(STD_INPUT_HANDLE);
  return read_from_pipe(&pr, time_out);
  }

#else

  #define MAX_SIZE 4096

/*
  create_pipe
  cmdLine:     executable name + parameters
  dir:         directory where executable resides (or nullptr: will look in current active directory)
  current_dir: if not nullptr, will change active directory to this one
  pr:          output process pointer
  */
int create_pipe(const char *cmdLine, const char *dir, const char* current_dir, void** pr)
  {
  *pr = nullptr;
  return -1;
  }

/*
destroy_pipe
signal = 9 : kill immediately
signal = 10: kill after 3s waiting
*/
void destroy_pipe(void* pr, int signal)
  {

  }

//returns 0 if successful
//message should be utf8 string
int send_to_pipe(void* process, const char* message)
  {
  return -1;
  }

//returns utf8 string
std::string read_from_pipe(void* process, int time_out)
  {
  return "";
  }



inline std::string read_std_input(int time_out)
  {
  std::stringstream ss;
  char buffer[MAX_SIZE];
  while (fgets(buffer, MAX_SIZE, stdin))
    {
    ss << buffer;
    }
  return ss.str();
  }

#endif

JAM_END