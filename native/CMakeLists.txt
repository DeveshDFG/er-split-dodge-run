cmake_minimum_required(VERSION 3.15.0)
project(separate_roll_and_sprint LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
add_definitions(-D_ITERATOR_DEBUG_LEVEL=0)

include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.3
)
FetchContent_MakeAvailable(minhook)

add_library(minhook_lib STATIC
    "${minhook_SOURCE_DIR}/src/buffer.c"
    "${minhook_SOURCE_DIR}/src/hook.c"
    "${minhook_SOURCE_DIR}/src/trampoline.c"
    "${minhook_SOURCE_DIR}/src/hde/hde64.c"
)
target_include_directories(minhook_lib PUBLIC "${minhook_SOURCE_DIR}/include")

set(_liber_root "")
if(DEFINED ENV{ER_SPLIT_LIBER_ROOT})
    set(_liber_candidate "$ENV{ER_SPLIT_LIBER_ROOT}")
    if(EXISTS "${_liber_candidate}/CMakeLists.txt")
        set(_liber_root "${_liber_candidate}")
    endif()
endif()
if(NOT _liber_root)
    foreach(_liber_candidate
        "${CMAKE_CURRENT_SOURCE_DIR}/libER/libER"
        "${CMAKE_CURRENT_SOURCE_DIR}/../libER/libER"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../EldenRingSplitDodgeRun/er-split-dodge-run/libER/libER")
        if(EXISTS "${_liber_candidate}/CMakeLists.txt")
            set(_liber_root "${_liber_candidate}")
            break()
        endif()
    endforeach()
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_cspc_symbols.cmake")
set(ER_SPLIT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ER_SPLIT_GENERATED_DIR}")
set(ER_SPLIT_CSPC_SYMBOLS_HEADER "${ER_SPLIT_GENERATED_DIR}/cspc_key_config_symbols.generated.hpp")
er_split_generate_cspc_symbols_header("${_liber_root}" "${ER_SPLIT_CSPC_SYMBOLS_HEADER}")

add_library(separate_roll_and_sprint SHARED separate_roll_and_sprint.cpp)
target_include_directories(separate_roll_and_sprint PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${ER_SPLIT_GENERATED_DIR}")
target_link_libraries(separate_roll_and_sprint PRIVATE minhook_lib)
set_target_properties(separate_roll_and_sprint PROPERTIES OUTPUT_NAME "separate_roll_and_sprint")

set(ER_SPLIT_BIND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bind")

add_custom_command(
    TARGET separate_roll_and_sprint
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/separate_roll_and_sprint.ini"
        "$<TARGET_FILE_DIR:separate_roll_and_sprint>/separate_roll_and_sprint.ini"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ER_SPLIT_BIND_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:separate_roll_and_sprint>"
        "${ER_SPLIT_BIND_DIR}/$<TARGET_FILE_NAME:separate_roll_and_sprint>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/separate_roll_and_sprint.ini"
        "${ER_SPLIT_BIND_DIR}/separate_roll_and_sprint.ini"
    COMMENT "Copy separate_roll_and_sprint.dll and INI to build output and bind/"
)

if(MSVC)
    add_custom_command(
        TARGET separate_roll_and_sprint
        POST_BUILD
        COMMAND dumpbin /DEPENDENTS "$<TARGET_FILE:separate_roll_and_sprint>"
        COMMENT "Verify separate_roll_and_sprint.dll has no libER dependency"
    )
endif()
