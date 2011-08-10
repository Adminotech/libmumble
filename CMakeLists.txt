cmake_minimum_required (VERSION 2.6)
include(FindPkgConfig)

# Project properties
project (libmumbleclient)
set (libmumbleclient_VERSION_MAJOR 0)
set (libmumbleclient_VERSION_MINOR 1)

set (CMAKE_BUILD_TYPE Release Debug)
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY src)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY src)

# Set this ON if you want to build a test executable.
# For the pure library this is not needed.
set (BUILD_TESTS OFF)
set (BUILD_CELT OFF)

# Set the following for windows to help cmake out a bit
# This is bad cmake but will have to do, remember forward slashes as separators /
if (WIN32)
    # Build celt on windows
    # Requires you to do a git submodules init and git submodules update
    set (BUILD_CELT ON)
    # Set depedency root directories
    set (Boost_USE_STATIC_LIBS ON)
    set (BOOST_ROOT C:/deps/Boost)
    set (OPENSSL_ROOT C:/deps/OpenSSL)
    set (PROTOBUF_ROOT C:/deps/protobuf)
    # Give cmake some hints where to look for libs/includes/executables in Find*.cmake helpers.
    # You can add paths here if the below are not maching to your setup.
    set (CMAKE_INCLUDE_PATH ${CMAKE_INCLUDE_PATH} ${OPENSSL_ROOT}/include ${PROTOBUF_ROOT}/include ${PROTOBUF_ROOT}/vsprojects/include ${PROTOBUF_ROOT}/src)
    set (CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${OPENSSL_ROOT}/lib ${PROTOBUF_ROOT}/lib ${PROTOBUF_ROOT}/vsprojects/lib ${PROTOBUF_ROOT}/vsprojects/Release)
    set (CMAKE_PROGRAM_PATH ${CMAKE_PROGRAM_PATH} ${PROTOBUF_ROOT}/bin ${PROTOBUF_ROOT}/lib ${PROTOBUF_ROOT}/vsprojects/bin ${PROTOBUF_ROOT}/vsprojects/Release)
endif ()

# Boost
set (Boost_USE_MULTITHREADED ON)
find_package (Boost 1.38.0 REQUIRED COMPONENTS system thread date_time regex)
message (STATUS "Boost found: " ${Boost_FOUND})
message (STATUS "Include dirs")
message (STATUS "-- " ${Boost_INCLUDE_DIRS})
message (STATUS "Libraries")
foreach (Boost_lib ${Boost_LIBRARIES})
    message (STATUS "-- " ${Boost_lib})
endforeach ()
message (STATUS "-- ")

# OpenSSL
find_package (OpenSSL REQUIRED)
message (STATUS "OpenSSL found: " ${OPENSSL_FOUND})
message (STATUS "Include dirs")
message (STATUS "-- " ${OPENSSL_INCLUDE_DIR})
message (STATUS "Libraries")
foreach (OpenSSL_lib ${OPENSSL_LIBRARIES})
    message (STATUS "-- " ${OpenSSL_lib})
endforeach ()
message (STATUS "-- ")

# Protobuf
find_package (Protobuf REQUIRED)
message (STATUS "Protobuf found: " ${PROTOBUF_FOUND})
message (STATUS "Include dirs")
message (STATUS "-- " ${PROTOBUF_INCLUDE_DIRS})
message (STATUS "Libraries")
message (STATUS "-- " ${PROTOBUF_LIBRARIES})
message (STATUS "Protoc executable")
message (STATUS "-- " ${PROTOBUF_PROTOC_EXECUTABLE})
message (STATUS "-- ")

# Platform spesific flags
if (NOT WIN32)
    # -Wmissing-noreturn -pthread
    set(COMMON_CFLAGS "-g -Wall -Wextra -Wfatal-errors -Wshadow -Wredundant-decls -Winit-self -Wunreachable-code -Wunused -Wunused-function -Wwrite-strings -Wfloat-equal -Wno-ignored-qualifiers -fvisibility=hidden")

    set(CMAKE_C_FLAGS_DEBUG  "${COMMON_CFLAGS} -pedantic -std=c99")
    set(CMAKE_CXX_FLAGS_DEBUG "${COMMON_CFLAGS} -Woverloaded-virtual -Wold-style-cast -Wstrict-null-sentinel -Woverloaded-virtual -fvisibility-inlines-hidden")
else()
	
    # note: /GL and /LTCG are not recommended for libraries that other people use
    # http://connect.microsoft.com/VisualStudio/feedback/details/560680/vs2008-link-fatal-error-c1900-il-mismatch-between-p1-version-20080116-and-p2-version-20070207

    set(COMMON_CLAGS "/W4")

    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${COMMON_CFLAGS}")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${COMMON_CFLAGS} /GL")

    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${COMMON_CFLAGS}")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${COMMON_CFLAGS} /GL")

    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")

    add_definitions(-D_WIN32_WINNT=0x0501)
endif()

# Generate protobuf files, this will be a custom step in your build
# the files are generated at first build if not there.
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS Mumble.proto)

# Project sources
set (LIBMUMBLE_SOURCES 
    src/client.cc 
    src/client_lib.cc 
    src/logging.cpp
    src/CryptState.cpp 
)

# Project includes
set (LIBMUMBLE_HEADERS 
    src/channel.h 
    src/client.h 
    src/client_lib.h 
    src/logging.h 
    src/messages.h 
    src/settings.h 
    src/user.h 
    src/visibility.h
    src/CryptState.h 
    src/PacketDataStream.h
)

# Dependency includes
include_directories(. ${Boost_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR} ${CELT_INCLUDE_DIR} ${PROTOBUF_INCLUDE_DIR})

# Default to shared lib on all platforms
set (LIBMUMBLE_BUILD_TYPE SHARED)
if (WIN32)
    set (LIBMUMBLE_BUILD_TYPE STATIC)
endif ()

# Windows spesific dllexport/dllimport separation
add_definitions(-DLIBMUMBLECLIENT_EXPORT_API)
if (${LIBMUMBLE_BUILD_TYPE} STREQUAL "SHARED")
    # Windows: If you link to libmumbleclient dynamically, 
    # you must define LIBMUMBLECLIENT_DYNAMIC in your project
    add_definitions(-DLIBMUMBLECLIENT_DYNAMIC)
else ()
    add_definitions(-DLIBMUMBLECLIENT_STATIC)
endif ()

# Add mumbleclient project
add_library(mumbleclient ${LIBMUMBLE_BUILD_TYPE} ${LIBMUMBLE_SOURCES} ${LIBMUMBLE_HEADERS} ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(mumbleclient ${Boost_LIBRARIES} ${OPENSSL_LIBRARIES} ${PROTOBUF_LIBRARY})
if(WIN32)
    set_target_properties (mumbleclient PROPERTIES DEBUG_POSTFIX d)
    set_target_properties (mumbleclient PROPERTIES PREFIX "")
endif()

# MP3 support
option(WITH_MPG123 "Add mp3 playback support" OFF)
if (WITH_MPG123)
	add_definitions(-DWITH_MPG123)
	set(LIBRARIES mpg123)
endif()

# Tests
if (BUILD_TESTS)
    pkg_check_modules(CELT celt>=0.7.0)
    # You can do a git submodules init and update to get this
    if (NOT CELT_FOUND)
        set (BUILD_CELT ON)
        set (CELT_LIBRARIES libcelt)
        set (CELT_INCLUDE_DIR celt/libcelt)
    endif ()
    add_executable (main src/main.cc)
    target_link_libraries (main mumbleclient ${LIBRARIES} ${CELT_LIBRARIES} ${Boost_LIBRARIES})
endif()

# Build celt
if (BUILD_CELT)
    add_subdirectory (celt-build)
endif ()

