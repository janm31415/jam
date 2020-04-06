#pragma once

#include "engine.h"
#include <iostream>
#include <string>

void save_to_stream(std::ostream& str, const app_state& state);

app_state load_from_stream(std::istream& str);

void save_to_file(const std::string& filename, const app_state& state);

app_state load_from_file(const std::string& filename);

void save_file_state_to_stream(std::ostream& str, const app_state& state);

void save_file_to_stream(std::ostream& str, const jamlib::file& f, bool command);

jamlib::app_state load_file_state_from_stream(std::istream& str);

jamlib::file load_file_from_stream(std::istream& str);