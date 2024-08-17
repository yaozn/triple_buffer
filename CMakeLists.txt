cmake_minimum_required(VERSION 3.18)

project(triple_buffer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17 -g")

#debug surport
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -D__DEBUG__")

SET(SRC 
    example.cpp 
)

add_executable(${PROJECT_NAME} ${SRC})

target_include_directories(${PROJECT_NAME}
PRIVATE
    ${REPO_ROOT}/include
    ${REPO_ROOT}/src
    ${REPO_ROOT}/lib
)

target_link_directories( ${PROJECT_NAME} 
PRIVATE
    ${REPO_ROOT}/lib/ctp
)

target_link_libraries(${PROJECT_NAME} 
PRIVATE 
    pthread
)

install (TARGETS ${PROJECT_NAME} DESTINATION .)