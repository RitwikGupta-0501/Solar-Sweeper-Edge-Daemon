cmake_minimum_required(VERSION 3.16)
project(SolarSweeperEdgeDaemon LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
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

# --- tinyfsm: header-only, wrap in INTERFACE target ---
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

# --- asio: standalone async I/O (header-only) ---
FetchContent_Declare(asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-30-2)
FetchContent_MakeAvailable(asio)
add_library(asio_headers INTERFACE)
target_include_directories(asio_headers INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio_headers INTERFACE ASIO_STANDALONE)

# --- paho.mqtt.c ---
FetchContent_Declare(paho_c
  GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.c.git
  GIT_TAG v1.3.13)
set(PAHO_WITH_SSL OFF CACHE BOOL "")
set(PAHO_BUILD_SHARED OFF CACHE BOOL "")
set(PAHO_BUILD_STATIC ON CACHE BOOL "")
set(PAHO_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(paho_c)

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(daemon_core STATIC
  src/Core/Flags.cpp
  src/Core/StateMachine.cpp
  src/Config/Config.cpp
  src/Hardware/SerialHardwareDriver.cpp
  src/Network/TelemetryCache.cpp
  src/Mocks/MockNetworkClient.cpp
  src/Mocks/MockHardwareDriver.cpp
  src/Mocks/MockVisionEngine.cpp
  src/Hardware/SerialHardwareDriver.cpp
  src/Network/MqttNetworkClient.cpp
  src/Network/TelemetrySpooler.cpp
  src/App/Application.cpp
  src/Util/Logger.cpp
)
target_include_directories(daemon_core PUBLIC include)
target_link_libraries(daemon_core PUBLIC
  tinyfsm
  concurrentqueue_headers
  nlohmann_json::nlohmann_json
  asio_headers
  paho-mqtt3a-static
  Threads::Threads
)

add_executable(solar_edge_daemon src/main.cpp)
target_link_libraries(solar_edge_daemon PRIVATE daemon_core)

enable_testing()
add_subdirectory(tests)
