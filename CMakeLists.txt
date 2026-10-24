cmake_minimum_required(VERSION 3.25)
project(ultratab VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

# Ultra Tab reuses lapis's session transport and launch identity, its Tab
# order header and its next-prompt helper, from the pinned submodule.
set(LAPIS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lapis" CACHE PATH "lapis checkout")
if(NOT EXISTS "${LAPIS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "lapis is missing: git submodule update --init")
endif()
set(LAPIS_BUILD_SESSION ON)
set(LAPIS_BUILD_DESKTOP OFF)
# Only the libraries Ultra Tab links are built; lapis's own tests are not.
set(BUILD_TESTING OFF)
add_subdirectory("${LAPIS_SOURCE_DIR}" lapis EXCLUDE_FROM_ALL)
unset(BUILD_TESTING)

add_subdirectory(mac)
