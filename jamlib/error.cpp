#include "error.h"
#include <sstream>

namespace jamlib
  {

  void throw_error(error_type t, std::string extra)
    {
    std::stringstream str;
    switch (t)
      {
      case bad_syntax:
        str << "Bad syntax";
        break;
      case no_tokens:
        str << "I expect more tokens in this command";
        break;
      case command_expected:
        str << "I expect a command";
        break;
      case address_expected:
        str << "I expect an address";
        break;
      case token_expected:
        str << "I expect a token";
        break;
      case invalid_regex:
        str << "Invalid regular expression";
        break;
      case pipe_error:
        str << "Pipe error";
        break;
      case invalid_address:
        str << "Invalid address";
        break;
      case not_implemented:
        str << "Not implemented";
        break;
      }
    if (!extra.empty())
      str << ": " << extra;
    throw std::runtime_error(str.str());
    }

  }