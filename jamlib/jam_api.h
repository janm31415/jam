#pragma once

#ifdef _WIN32
#if defined(jamlib_EXPORTS)
#  define JAMLIB_API __declspec(dllexport)
#else
#  define JAMLIB_API __declspec(dllimport)
#endif
#else
#  define JAMLIB_API
#endif