#pragma once

#include "jam_api.h"

#include <stdint.h>
#include <string>
#include <immutable/vector.h>
#include <vector>
#include <optional>
#include <ostream>

#include "encoding.h"

namespace jamlib
  {

  typedef immutable::vector<wchar_t, false, 5> buffer;

  struct file;

  struct range
    {
    int64_t p1, p2;
    };

  struct address
    {
    range r;
    uint64_t file_id;
    };

  struct snapshot
    {
    buffer content;
    address dot;
    uint64_t modification_mask;
    encoding enc;
    };

  struct file
    {
    buffer content;
    std::string filename;
    uint64_t modification_mask;
    uint64_t file_id;
    address dot;
    immutable::vector<snapshot, false> history;
    uint64_t undo_redo_index;
    encoding enc;
    };

  struct app_state
    {
    std::vector<file> files;
    uint64_t active_file;
    };

  //use const cast to convert your main's char** argv to const char** argv
  JAMLIB_API app_state init_state(int argc, const char** argv);
  JAMLIB_API std::optional<app_state> handle_command(app_state state, std::string command);
  JAMLIB_API void parse_command(std::string& executable_name, std::string& folder, std::vector<std::string>& parameters, std::string command);

  //nullptr for wcout, which is the default
  JAMLIB_API void set_output_stream(std::wostream* output);
  }