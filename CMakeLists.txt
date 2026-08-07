cmake_minimum_required(VERSION 3.16)
project(SolarSweeperEdgeDaemon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Threads REQUIRED)
include(FetchContent)

# --- nlohmann/json: config parsing + (later) MQTT payloads ---
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3)
FetchContent_MakeAvailable(nlohmann_json)

# --- moodycamel::ConcurrentQueue / BlockingConcurrentQueue: header-only, no CMake target exported ---
FetchContent_Declare(concurrentqueue
  GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
  GIT_TAG v1.0.4)
FetchContent_MakeAvailable(concurrentqueue)
add_library(concurrentqueue_headers INTERFACE)
target_include_directories(concurrentqueue_headers INTERFACE ${concurrentqueue_SOURCE_DIR})

# --- tinyfsm: header-only, and unlike the others ships no CMakeLists.txt
# at all (just a bare include/ tree), so there's no target to make
# available -- fetch the source only and wrap it in our own INTERFACE
# target. FetchContent_Populate is deprecated in favor of
# FetchContent_MakeAvailable, but that requires add_subdirectory()-able
# project files, which tinyfsm doesn't provide; the policy scoping below
# uses the old (still-supported) behavior deliberately, not by oversight.
if(POLICY CMP0169)
  cmake_policy(PUSH)
  cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_Declare(tinyfsm
  GIT_REPOSITORY https://github.com/digint/tinyfsm.git
  GIT_TAG v0.3.3)
FetchContent_Populate(tinyfsm)
if(POLICY CMP0169)
  cmake_policy(POP)
endif()
add_library(tinyfsm INTERFACE)
target_include_directories(tinyfsm INTERFACE ${tinyfsm_SOURCE_DIR}/include)

# --- Catch2 v3: unit tests ---
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.4)
FetchContent_MakeAvailable(Catch2)

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(daemon_core STATIC
  src/Core/Flags.cpp
  src/Core/StateMachine.cpp
  src/Config/Config.cpp
  src/Mocks/MockNetworkClient.cpp
  src/Mocks/MockHardwareDriver.cpp
  src/Mocks/MockVisionEngine.cpp
  src/App/Application.cpp
  src/Util/Logger.cpp
)
target_include_directories(daemon_core PUBLIC include)
target_link_libraries(daemon_core PUBLIC
  tinyfsm
  concurrentqueue_headers
  nlohmann_json::nlohmann_json
  Threads::Threads
)

add_executable(solar_edge_daemon src/main.cpp)
target_link_libraries(solar_edge_daemon PRIVATE daemon_core)

enable_testing()
add_subdirectory(tests)
