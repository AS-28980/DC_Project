cmake_minimum_required(VERSION 3.10)
project(blockdag_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(blockdag_sim
    src/main.cpp
    src/tangle_sim.cpp
    src/witness_sim.cpp
)
