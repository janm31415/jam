#include "encoding.h"
#include <utils/encoding.h>
#include <map>
#include <vector>
#include <fstream>

namespace jamlib
  {

  std::wstring convert_string_to_wstring(const std::string& str, encoding enc)
    {
    std::wstring out;

    switch (enc)
      {
      case ENC_ASCII:
      {
      out.reserve(str.size());
      for (const auto& ch : str)
        out.push_back(ch);
      break;
      }
      default:
      {
      out = JAM::convert_string_to_wstring(str);
      break;
      }
      }
    return out;
    }

  std::string convert_wstring_to_string(const std::wstring& str, encoding enc)
    {    
    std::string out;
    switch (enc)
      {
      case ENC_ASCII:
      {
      out.reserve(str.size());
      for (const auto& ch : str)
        out.push_back((unsigned char)ch);
      break;
      }
      default:
      {
      out = JAM::convert_wstring_to_string(str);
      break;
      }
      }
    return out;
    }

  }