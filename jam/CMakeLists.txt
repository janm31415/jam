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
    ${CMAKE_CURRENT_SOURCE_DIR}/../SDL2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../SDL2_ttf/
    )	
	
target_link_libraries(jam
    PRIVATE 
    jamlib
    pdcurses
    SDL2
    SDL2main
    SDL2_ttf
    )	


add_custom_command(TARGET jam POST_BUILD 
   COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/comments.json" "${CMAKE_CURRENT_BINARY_DIR}/comments.json")


add_custom_command(TARGET jam POST_BUILD 
   COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/font" "${CMAKE_CURRENT_BINARY_DIR}/font")