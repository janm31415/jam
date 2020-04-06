#pragma once

#include <string>

enum error_type
  {
  string_too_long,
  could_not_send_through_pipe
  };


void throw_error(error_type t, std::string extra = std::string(""));