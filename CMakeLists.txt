cmake_minimum_required(VERSION 2.8.11)
project (jam)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")
set(CMAKE_PDB_OUTPUT_DIRECTORY     "${CMAKE_CURRENT_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_subdirectory(immutable)
add_subdirectory(immutable.tests)
add_subdirectory(jamcmd)
add_subdirectory(jamlib)
add_subdirectory(jamlib.tests)
add_subdirectory(utils)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
