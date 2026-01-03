cmake_minimum_required(VERSION 3.22)

project(CompassCompressor VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# JUCE LOCATION (ON INTERNAL DISK)
set(JUCE_DIR "/Users/johncollins/JUCE")

add_subdirectory("${JUCE_DIR}" JUCE)
add_subdirectory(Source)
