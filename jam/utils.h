#pragma once

#include "engine.h"

bool ask_user_to_save_modified_file(const jamlib::file& f);
bool is_modified(const jamlib::file& f);

std::string cleanup(std::string cmd);

std::string resolve_regex_escape_characters(const std::string& cmd);

std::string resolve_jamlib_escape_characters(const std::string& cmd);

std::string remove_quotes_from_path(std::string str);

std::string get_file_in_executable_path(const std::string& filename);

std::string flip_backslash_to_slash_in_filename(const std::string& filename);


/*
Input is a filename and the filename of the window.
This method will look for filename in the folder defined by the window_filename, or the executable path.
Returns empty string if nothing was found, or returns the path of the file.
*/
std::string get_file_path(const std::string& filename, const std::string& window_filename);