#pragma once

#include <string>

namespace jamlib
  {
 
  int StartChildProcessWithoutPipes(const char *cmdLine, const char *dir, const char* current_dir, void** pr);


  }