#pragma once

#include "namespace.h"

#ifdef _WIN32
#include <windows.h>
#include "encoding.h"
#include <string>
#else
#include <unistd.h>
#endif

#include "active_folder.h"

JAM_BEGIN

#ifdef _WIN32

struct process_info
  {
  HANDLE hProcess;
  DWORD pid;
  HANDLE hTo;
  HANDLE hFrom;
  };

int run_process(const char *path, const char** argv, const char* current_dir, void** pr)
  {
  STARTUPINFOW siStartInfo;
  JAM::process_info *cp;
  DWORD err;
  BOOL fSuccess;
  PROCESS_INFORMATION piProcInfo;

  *pr = nullptr;

  active_folder af(current_dir);

  std::wstring wcmdLine;  
  wcmdLine = convert_string_to_wstring(std::string(path));

  // i = 0 equals path, is for linux
  size_t i = 1;
  const char* arg = argv[i];
  while (arg)
    {
    wcmdLine.append(L" " + convert_string_to_wstring(std::string(arg)));
    arg = argv[++i];
    }
  /* Now create the child process. */

  siStartInfo.cb = sizeof(STARTUPINFOW);
  siStartInfo.lpReserved = NULL;
  siStartInfo.lpDesktop = NULL;
  siStartInfo.lpTitle = NULL;
  siStartInfo.dwFlags = 0;
  siStartInfo.cbReserved2 = 0;
  siStartInfo.lpReserved2 = NULL;
  siStartInfo.hStdInput = NULL;
  siStartInfo.hStdOutput = NULL;
  siStartInfo.hStdError = NULL;

  fSuccess = CreateProcessW(NULL,
    (LPWSTR)(wcmdLine.c_str()),	   /* command line */
    NULL,	   /* process security attributes */
    NULL,	   /* primary thread security attrs */
    TRUE,	   /* handles are inherited */
    CREATE_NEW_CONSOLE,
    NULL,	   /* use parent's environment */
    NULL,
    &siStartInfo, /* STARTUPINFO pointer */
    &piProcInfo); /* receives PROCESS_INFORMATION */

  err = GetLastError();

  if (!fSuccess) 
    {
    return err;
    }

  SetPriorityClass(piProcInfo.hProcess, 0x00000080);

  /* Close the handles we don't need in the parent */
  CloseHandle(piProcInfo.hThread);

  /* Prepare return value */
  cp = (JAM::process_info *)calloc(1, sizeof(JAM::process_info));
  cp->hProcess = piProcInfo.hProcess;
  cp->pid = piProcInfo.dwProcessId;
  cp->hFrom = NULL;
  cp->hTo = NULL;

  *pr = (void *)cp;

  return NO_ERROR;
  }

inline void destroy_process(void* pr, int signal)
  {
  process_info *cp;
  int result;

  cp = (process_info *)pr;
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

#else

inline int run_process(const char *path, const char** argv, const char* current_dir, int* process)
  {
  pid_t pid = 0;
  
  pid = fork();
  if (pid < 0)
    throw std::runtime_error("failed to fork");
  if (pid == 0)
    {    
    prctl(PR_SET_PDEATHSIG, SIGTERM);

    if (execv(path, argv) == -1)
      throw std::runtime_error("failed to pipe (execl failed)");
    exit(1);
    }
  
  *process = pid;

  return 0;
  }

inline void destroy_process(int* process, int)
  {
  kill(*process, SIGKILL);
  int status;
  waitpid(*process, &status, 0);
  }

#endif

JAM_END