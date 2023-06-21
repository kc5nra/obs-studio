project(tracy)

if(ENABLE_TRACY)
  set(TRACY_ENABLE 1)
else()
  set(TRACY_ENABLE 0)
endif()

include(FetchContent)

FetchContent_Declare(
  Tracy
  GIT_REPOSITORY https://github.com/wolfpld/tracy
  GIT_TAG v0.9.1
  FIND_PACKAGE_ARGS)

if(NOT OS_WINDOWS)
set(CMAKE_CXX_FLAGS_OLD "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter -Wno-deprecated-declarations")
endif()
FetchContent_MakeAvailable(Tracy)
if(NOT OS_WINDOWS)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_OLD}")
endif()

add_library(tracy_interface INTERFACE)
add_library(OBS::tracy_interface ALIAS tracy_interface)

target_compile_definitions(tracy_interface INTERFACE TRACY_MANUAL_LIFETIME NOMINMAX)
if(NOT OS_MACOS)
  target_compile_definitions(tracy_interface INTERFACE TRACY_DELAYED_INIT)
endif()

if(ENABLE_TRACY)
  target_compile_definitions(tracy_interface INTERFACE TRACY_ENABLE TRACY_IMPORTS)
  target_compile_definitions(TracyClient PUBLIC TRACY_MANUAL_LIFETIME TRACY_EXPORTS)
endif()

target_include_directories(tracy_interface INTERFACE ${Tracy_SOURCE_DIR}/public)

add_library(OBS::tracy ALIAS TracyClient)
