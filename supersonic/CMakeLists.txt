# CMakeList.txt : CMake project for supersonic, include source and define
# project specific logic here.
#

# set C++23 standard
set(CMAKE_CXX_STANDARD 23)

# Function to add an executable with common settings
function(add_custom_executable target_name source_file)
    add_executable(${target_name} ${source_file} saudio.cpp config.cpp crc.cpp smac.cpp tun.cpp)
    # target_compile_options(${target_name} PUBLIC -g -Wall -Wextra -Wpedantic -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
    # target_link_options(${target_name} PUBLIC -g -fsanitize=address -fsanitize=undefined)
    target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR}/libs/AudioFile ${CMAKE_SOURCE_DIR}/libs/code ${CMAKE_SOURCE_DIR}/libs/miniaudio ${CMAKE_SOURCE_DIR}/libs/wintun/include)
    target_link_libraries(${target_name} PRIVATE Jack::Jack fmt::fmt Boost::json Boost::asio Boost::unit_test_framework Boost::lockfree Boost::crc kissfft::kissfft spdlog::spdlog cxxopts::cxxopts iphlpapi kernel32 ntdll ws2_32)
endfunction()

# Add executables using the custom function
add_custom_executable(proj0 proj0.cpp)
add_custom_executable(play_and_record play_and_record.cpp)
add_custom_executable(p1t2 p1t2.cpp)
add_custom_executable(proj1 proj1.cpp)
add_custom_executable(proj2 proj2.cpp)
add_custom_executable(proj3 proj3.cpp)
add_custom_executable(unittest unittest.cpp)
add_custom_executable(sendrecv sendrecv.cpp)