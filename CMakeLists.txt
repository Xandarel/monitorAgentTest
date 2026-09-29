cmake_minimum_required(VERSION 3.16)
project(monitor_agent VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "monitor_agent targets Windows (WinAPI).")
endif()

add_executable(monitor_agent
  src/main.cpp
  src/console.cpp
  src/encoding.cpp
  src/json_util.cpp
  src/collector.cpp
  src/http_client.cpp
  src/storage.cpp
)

target_include_directories(monitor_agent PRIVATE src)
target_link_libraries(monitor_agent PRIVATE user32 winhttp shell32)
target_compile_definitions(monitor_agent PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)

if(MSVC)
  target_compile_options(monitor_agent PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(monitor_agent PRIVATE -Wall -Wextra -Wpedantic -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# Иначе Visual Studio по умолчанию запускает служебный ALL_BUILD, у которого нет exe.
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT monitor_agent)
set_target_properties(monitor_agent PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
