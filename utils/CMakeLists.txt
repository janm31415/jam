
set(HDRS
jam_active_folder.h
jam_encoding.h
jam_exepath.h
jam_filename.h
jam_file_utils.h
jam_namespace.h
jam_pipe.h
jam_process.h
jam_utf8.h
jam_utf8_checked.h
jam_utf8_core.h
jam_utf8_unchecked.h
)
	
set(SRCS
utils.cpp
)

if (WIN32)
set(CMAKE_C_FLAGS_DEBUG "/W4 /MP /GF /RTCu /Od /MDd /Zi")
set(CMAKE_CXX_FLAGS_DEBUG "/W4 /MP /GF /RTCu /Od /MDd /Zi")
set(CMAKE_C_FLAGS_RELEASE "/W4 /MP /GF /O2 /Ob2 /Oi /Ot /MD /Zi /DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "/W4 /MP /GF /O2 /Ob2 /Oi /Ot /MD /Zi /DNDEBUG")
endif (WIN32)

# general build definitions
add_definitions(-D_SCL_SECURE_NO_WARNINGS)

add_library(utils STATIC ${HDRS} ${SRCS})
source_group("Header Files" FILES ${HDRS})
source_group("Source Files" FILES ${SRCS})

target_include_directories(utils
    PRIVATE
    )	
	
target_link_libraries(utils
    PRIVATE	
    )	