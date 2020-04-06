#pragma once

#include <string>

#include "jam_api.h"

namespace jamlib
  {

  enum encoding
    {
    ENC_ASCII,
    ENC_UTF8
    };

  JAMLIB_API std::wstring convert_string_to_wstring(const std::string& str, encoding enc);

  JAMLIB_API std::string convert_wstring_to_string(const std::wstring& str, encoding enc);

  JAMLIB_API uint16_t ascii_to_utf16(unsigned char ch);
  }