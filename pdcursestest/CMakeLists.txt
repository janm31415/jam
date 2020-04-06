set(SDL_INCLUDE_DIR "C:/Program Files/SDL2/include")
set(SDL_LIBRARY "C:/Program Files/SDL2/lib")
find_package(SDL REQUIRED)

include_directories(${SDL_INCLUDE_DIR}/SDL2)
link_directories(${SDL_LIBRARY})

set(HDRS
    )
	
set(SRCS
main.cpp
)

set(CMAKE_C_FLAGS_DEBUG "/W4 /MP /GF /RTCu /Od /MDd /Zi")
set(CMAKE_CXX_FLAGS_DEBUG "/W4 /MP /GF /RTCu /Od /MDd /Zi")
set(CMAKE_C_FLAGS_RELEASE "/W4 /MP /GF /O2 /Ob2 /Oi /Ot /MD /Zi")
set(CMAKE_CXX_FLAGS_RELEASE "/W4 /MP /GF /O2 /Ob2 /Oi /Ot /MD /Zi")

# general build definitions
add_definitions(-D_SCL_SECURE_NO_WARNINGS)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

add_definitions(-DPDC_DLL_BUILD)
add_definitions(-DPDC_RGB)

add_executable(pdcursestest WIN32 ${HDRS} ${SRCS})
source_group("Header Files" FILES ${hdrs})
source_group("Source Files" FILES ${srcs})

 target_include_directories(pdcursestest
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
	  ${CMAKE_CURRENT_SOURCE_DIR}/../pdcurses/
    )	
	
target_link_libraries(pdcursestest
    PRIVATE
    pdcurses
    SDL2.lib
    SDL2main.lib  
    )	