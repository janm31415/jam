#pragma once

#include <string>
#include <vector>

std::string get_executable_path();
bool file_exists(const std::string& filename);
std::string get_extension(const std::string& filename);
std::string remove_extension(const std::string& filename);

std::string get_folder(const std::string& path);
std::string get_filename(const std::string& path);

struct dir_iterator;
typedef dir_iterator* directory_iterator;

directory_iterator open_directory(const std::string& directory);
bool is_valid(directory_iterator iter);
std::string get_filename(directory_iterator iter);
void next_filename(directory_iterator iter);
void close_directory(directory_iterator& iter);

bool is_directory(directory_iterator iter);
bool is_file(directory_iterator iter);
bool is_directory(const std::string& directory);

std::vector<std::string> get_files_from_directory(const std::string& dir, bool include_subfolders);
std::vector<std::string> get_subdirectories_from_directory(const std::string& dir, bool include_subfolders);
std::vector<std::string> get_list_from_directory(const std::string& dir, bool include_subfolders);
