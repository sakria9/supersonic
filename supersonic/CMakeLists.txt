# CMakeList.txt : CMake project for supersonic, include source and define
# project specific logic here.
#

# Add source to this project's executable.

# set C++23 standard
set(CMAKE_CXX_STANDARD 23)

add_executable (proj0 "proj0.cpp" )
target_include_directories(proj0 PRIVATE ${CMAKE_SOURCE_DIR}/libs/AudioFile)
target_link_libraries(proj0 PRIVATE Jack::Jack fmt::fmt Boost::lockfree spdlog::spdlog cxxopts::cxxopts)
