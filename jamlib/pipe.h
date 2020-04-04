#pragma once

#include <string>

namespace jamlib
  {

  int StartChildProcess(const char *cmdLine, const char *dir, const char* current_dir, void** pr);
  int StartChildProcessWithoutPipes(const char *cmdLine, const char *dir, const char* current_dir, void** pr);

  void DestroyChildProcess(void* pr, int signal);
  
  void SendToProgram(const char* message, void* process);
  std::string ReadFromProgram(void* process, int time_out=100);
  }