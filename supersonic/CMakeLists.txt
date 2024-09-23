# CMakeList.txt : CMake project for supersonic, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (supersonic "supersonic.cpp" )

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET supersonic PROPERTY CXX_STANDARD 20)
endif()

target_include_directories(supersonic PRIVATE ${CMAKE_SOURCE_DIR}/libs/AudioFile)
target_link_libraries(supersonic PRIVATE Jack::Jack fmt::fmt Boost::lockfree)

# TODO: Add tests and install targets if needed.
