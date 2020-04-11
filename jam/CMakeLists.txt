if (WIN32)

set(SDL_INCLUDE_DIR "C:/Program Files/SDL2/include/SDL2/")
set(SDL_LIBRARIES "C:/Program Files/SDL2/lib/SDL2.lib" "C:/Program Files/SDL2/lib/SDL2main.lib")

set(SDL_TTF_INCLUDE_DIR "C:/Program Files/SDL_ttf/include/SDL2")
set(SDL_TTF_LIBRARIES "C:/Program Files/SDL_ttf/lib/SDL2_ttf.lib" "C:/Program Files/SDL_ttf/lib/freetype248.lib")

endif (WIN32)

if (UNIX)
  set(SDL_INCLUDE_DIR "/usr/include/SDL2/")
  set(SDL_LIBRARIES "/usr/lib/x86_64-linux-gnu/libSDL2-2.0.so" "/usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0")
  set(SDL_TTF_INCLUDE_DIR "/usr/include/SDL2/")
  set(SDL_TTF_LIBRARIES "/usr/lib/x86_64-linux-gnu/libSDL2_ttf-2.0.so" "/usr/lib/x86_64-linux-gnu/libSDL2_ttf-2.0.so.0")
endif (UNIX)

set(HDRS
async_messages.h
clipboard.h
colors.h
engine.h
error.h
grid.h
keyboard.h
mouse.h
pdcex.h
pref_file.h
resource.h
serialize.h
settings.h
syntax_highlight.h
utils.h
window.h
    )
	
set(SRCS
clipboard.cpp
colors.cpp
engine.cpp
error.cpp
grid.cpp
jam.rc
keyboard.cpp
main.cpp
mouse.cpp
pdcex.cpp
pref_file.cpp
serialize.cpp
settings.cpp
syntax_highlight.cpp
utils.cpp
window.cpp
)

set(JSON
${CMAKE_CURRENT_SOURCE_DIR}/../json/json.hpp
)

if (WIN32)
set(CMAKE_C_FLAGS_DEBUG "/W4 /MP /GF /RTCu /Od /MDd /Zi")
set(CMAKE_CXX_FLAGS_DEBUG "/W4 /MP /GF /RTCu /Od /MDd /Zi")
set(CMAKE_C_FLAGS_RELEASE "/W4 /MP /GF /O2 /Ob2 /Oi /Ot /MD /Zi")
set(CMAKE_CXX_FLAGS_RELEASE "/W4 /MP /GF /O2 /Ob2 /Oi /Ot /MD /Zi")
endif (WIN32)

# general build definitions
add_definitions(-D_SCL_SECURE_NO_WARNINGS)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

if (WIN32)
add_definitions(-DPDC_DLL_BUILD)
endif (WIN32)
add_definitions(-DPDC_RGB)
add_definitions(-DPDC_FORCE_UTF8)
add_definitions(-DPDC_WIDE)

if (WIN32)
add_executable(jam WIN32 ${HDRS} ${SRCS} ${JSON})
endif (WIN32)

if (UNIX)
add_executable(jam ${HDRS} ${SRCS} ${JSON})
endif (UNIX)

source_group("Header Files" FILES ${HDRS})
source_group("Source Files" FILES ${SRCS})
source_group("ThirdParty/json" FILES ${JSON})

 target_include_directories(jam
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/../utils/
    ${CMAKE_CURRENT_SOURCE_DIR}/../pdcurses/
    ${CMAKE_CURRENT_SOURCE_DIR}/../json/	 
    ${SDL_INCLUDE_DIR}
    ${SDL_TTF_INCLUDE_DIR}
    )	
	
target_link_libraries(jam
    PRIVATE 
    jamlib
    pdcurses
    ${SDL_LIBRARIES}
    ${SDL_TTF_LIBRARIES}
    )	


add_custom_command(TARGET jam POST_BUILD 
   COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/comments.json" "${CMAKE_CURRENT_BINARY_DIR}/comments.json")


add_custom_command(TARGET jam POST_BUILD 
   COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/font" "${CMAKE_CURRENT_BINARY_DIR}/font")