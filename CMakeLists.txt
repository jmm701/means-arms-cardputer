cmake_minimum_required(VERSION 3.16)
project(MeansArmsZero VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)

add_library(means_core STATIC
    src/calculator.cpp
    src/storage.cpp
)
target_include_directories(means_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(means_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(means-arms-zero
    src/main.cpp
)
target_link_libraries(means-arms-zero PRIVATE means_core ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES})
target_include_directories(means-arms-zero PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
target_compile_options(means-arms-zero PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(means_arms_tests tests/test_calculator.cpp)
target_link_libraries(means_arms_tests PRIVATE means_core)
add_test(NAME means_arms_tests COMMAND means_arms_tests)
