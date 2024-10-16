# CMakeList.txt : CMake project for supersonic, include source and define
# project specific logic here.
#

# set C++23 standard
set(CMAKE_CXX_STANDARD 23)

# Function to add an executable with common settings
function(add_custom_executable target_name source_file)
    add_executable(${target_name} ${source_file})
    target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR}/libs/AudioFile)
    target_link_libraries(${target_name} PRIVATE Jack::Jack fmt::fmt Boost::asio Boost::unit_test_framework Boost::lockfree kissfft::kissfft spdlog::spdlog cxxopts::cxxopts)
endfunction()

# Add executables using the custom function
add_custom_executable(proj0 proj0.cpp)
add_custom_executable(play_and_record play_and_record.cpp)
add_custom_executable(p1t2 p1t2.cpp)
add_custom_executable(proj1 proj1.cpp)
add_custom_executable(unittest unittest.cpp)