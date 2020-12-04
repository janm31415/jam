set(HDRS
    )
	
set(SRCS
main.cpp
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

if (WIN32)
add_executable(pdcursestest WIN32 ${HDRS} ${SRCS})
endif (WIN32)

if (UNIX)
add_executable(pdcursestest ${HDRS} ${SRCS})
endif (UNIX)

source_group("Header Files" FILES ${hdrs})
source_group("Source Files" FILES ${srcs})

target_include_directories(pdcursestest
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
	  ${CMAKE_CURRENT_SOURCE_DIR}/../pdcurses/
	  ${CMAKE_CURRENT_SOURCE_DIR}/../SDL2/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../SDL2_ttf/
    )	
	
target_link_libraries(pdcursestest
    PRIVATE
    pdcurses
    SDL2
    SDL2main
    SDL2_ttf
    )	