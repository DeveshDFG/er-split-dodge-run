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

add_library(separate_roll_and_sprint SHARED separate_roll_and_sprint.cpp)
target_include_directories(separate_roll_and_sprint PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(separate_roll_and_sprint PRIVATE minhook_lib)
set_target_properties(separate_roll_and_sprint PROPERTIES OUTPUT_NAME "separate_roll_and_sprint")

add_custom_command(
    TARGET separate_roll_and_sprint
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/separate_roll_and_sprint.ini"
        "$<TARGET_FILE_DIR:separate_roll_and_sprint>/separate_roll_and_sprint.ini"
    COMMENT "Copy separate_roll_and_sprint.ini next to DLL"
)

if(MSVC)
    add_custom_command(
        TARGET separate_roll_and_sprint
        POST_BUILD
        COMMAND dumpbin /DEPENDENTS "$<TARGET_FILE:separate_roll_and_sprint>"
        COMMENT "Verify separate_roll_and_sprint.dll has no libER dependency"
    )
endif()
