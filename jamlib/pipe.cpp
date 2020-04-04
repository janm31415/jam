#include "pipe.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <sstream>
#include <chrono>
#include <thread>

#include "error.h"

#include "encoding.h"

#include <utils/pipe.h>

namespace jamlib
  {

#ifdef _WIN32
  int StartChildProcessWithoutPipes(const char *cmdLine, const char *dir, const char* current_dir, void** pr)
    {
    STARTUPINFOW siStartInfo;
    JAM::child_proc *cp;
    wchar_t buf[MAX_PATH];
    DWORD err;
    BOOL fSuccess;
    PROCESS_INFORMATION piProcInfo;

    *pr = nullptr;

    GetCurrentDirectoryW(MAX_PATH, buf);

    if (current_dir)
      {
      std::wstring wdir(convert_string_to_wstring(std::string(current_dir), jamlib::ENC_UTF8));
      SetCurrentDirectoryW(wdir.c_str());
      }

    std::wstring wcmdLine;
    if (dir)
      {
      wcmdLine = convert_string_to_wstring(std::string(dir), jamlib::ENC_UTF8);
      wcmdLine.append(convert_string_to_wstring(std::string(cmdLine), jamlib::ENC_UTF8));
      }
    else
      wcmdLine = convert_string_to_wstring(std::string(cmdLine), jamlib::ENC_UTF8);
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
    SetCurrentDirectoryW(buf); /* return to prev directory */
    if (!fSuccess) {
      return err;
      }

    SetPriorityClass(piProcInfo.hProcess, 0x00000080);

    /* Close the handles we don't need in the parent */
    CloseHandle(piProcInfo.hThread);

    /* Prepare return value */
    cp = (JAM::child_proc *)calloc(1, sizeof(JAM::child_proc));
    cp->hProcess = piProcInfo.hProcess;
    cp->pid = piProcInfo.dwProcessId;
    cp->hFrom = NULL;
    cp->hTo = NULL;

    *pr = (void *)cp;

    return NO_ERROR;
    }
#else
  int StartChildProcessWithoutPipes(const char *cmdLine, const char *dir, const char* current_dir, void** pr)
    {
    *pr = nullptr;
    return -1;
    }
#endif

  int StartChildProcess(const char *cmdLine, const char *dir, const char* current_dir, void** pr)
    {
    return JAM::create_pipe(cmdLine, dir, current_dir, pr);
    }

  void DestroyChildProcess(void* pr, int signal)
    {
    JAM::destroy_pipe(pr, signal);
    }

  void SendToProgram(const char* message, void* process)
    {
    int res = JAM::send_to_pipe(process, message);
    if (res != 0)
      {
      std::stringstream str;
      str << "writing " << message << " to program: " << res;
      throw_error(pipe_error, str.str());
      }
    }

  std::string ReadFromProgram(void* process, int time_out)
    {
    return JAM::read_from_pipe(process, time_out);
    }

  }