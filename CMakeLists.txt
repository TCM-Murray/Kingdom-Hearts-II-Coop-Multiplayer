cmake_minimum_required(VERSION 3.20)
project(kh2coop C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the DLL needs no VC++ redistributable on the friend's PC.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MinHook (BSD-2-Clause, github.com/TsudaKageyu/minhook): inline function hooks.
include(FetchContent)
FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4)
FetchContent_GetProperties(minhook)
if(NOT minhook_POPULATED)
    FetchContent_Populate(minhook)
endif()
add_library(minhook_lib STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook_lib PUBLIC ${minhook_SOURCE_DIR}/include)

add_library(kh2coop SHARED src/dll/kh2coop.cpp src/dll/avatar_link.cpp src/dll/puppet.cpp src/dll/crash_log.cpp src/dll/save_guard.cpp src/dll/watch.cpp src/dll/aggro.cpp src/dll/follow.cpp src/dll/world_sync.cpp src/dll/pause_sync.cpp src/dll/spawn_sync.cpp src/dll/downed.cpp src/dll/game_over.cpp src/dll/load_barrier.cpp src/dll/party_hud.cpp src/dll/cutscene_twin.cpp src/dll/spawn_step.cpp src/dll/scene_sync.cpp)
target_link_libraries(kh2coop PRIVATE xinput ws2_32 minhook_lib)
target_compile_definitions(kh2coop PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(kh2coop PRIVATE /W4 /permissive-)
