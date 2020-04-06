#include "error.h"
#include <sstream>

void throw_error(error_type t, std::string extra)
  {
  std::stringstream str;
  switch (t)
    {
    case string_too_long:
      str << "String too long";
      break;
    case could_not_send_through_pipe:
      str << "Could not send message through the pipe";
      break;
    }
  if (!extra.empty())
    str << ": " << extra;
  throw std::runtime_error(str.str());
  }