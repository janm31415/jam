#pragma once

#include <string>

namespace jamlib
  {

  enum error_type
    {
    bad_syntax,
    no_tokens,
    command_expected,
    address_expected,
    token_expected,
    invalid_address,
    invalid_regex,
    pipe_error,
    not_implemented
    };


  void throw_error(error_type t, std::string extra = std::string(""));

  }