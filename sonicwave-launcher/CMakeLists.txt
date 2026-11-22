cmake_minimum_required(VERSION 3.22)
project(sonicwave-launcher LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG        v5.0.0
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(ftxui)
find_package(Threads REQUIRED)
add_executable(sw-launcher src/main.cpp)
target_link_libraries(sw-launcher PRIVATE
  ftxui::screen ftxui::dom ftxui::component Threads::Threads)