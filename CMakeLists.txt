# Standalone build: tests, the ABI conformance host and the screenshot
# harness. Games do not use this file; they include recomp_ui.cmake.
cmake_minimum_required(VERSION 3.20)
project(recomp_launcher LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/recomp_ui.cmake")
find_package(SDL3 REQUIRED CONFIG)

include(CTest)

# Core unit tests: no SDL, no GL.
add_executable(r4l-core-tests
    tests/test_core.cpp
    src/r4l/core/binds.cpp src/r4l/core/ini.cpp src/r4l/core/mods.cpp
    src/r4l/core/quality.cpp src/r4l/core/session.cpp
    src/r4l/core/json.cpp src/r4l/core/skin_model.cpp src/r4l/core/surface.cpp
    src/r4l/core/discscan.cpp src/r4l/core/discfs.cpp src/r4l/core/png.cpp src/r4l/core/memcard.cpp)
target_include_directories(r4l-core-tests PRIVATE src)
add_test(NAME r4l_core COMMAND r4l-core-tests ${CMAKE_CURRENT_SOURCE_DIR}/assets/skins)

# Fake host shared by the conformance test and the screenshot harness.
add_executable(r4l-skin-render tests/fake_host.cpp tests/abi_conformance.c)
target_link_libraries(r4l-skin-render PRIVATE SDL3::SDL3)
recomp_target_launcher_ui(r4l-skin-render CONSOLE psx)
add_test(NAME r4l_abi_symbols COMMAND r4l-skin-render --abi-selftest)
set_tests_properties(r4l_abi_symbols PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy")

# Struct layout identity against a recomp-ui checkout (skipped when absent).
set(R4L_RECOMP_UI_REFERENCE "" CACHE PATH "recomp-ui checkout to compare the ABI against")
if(R4L_RECOMP_UI_REFERENCE)
    add_test(NAME r4l_abi_layout
        COMMAND ${CMAKE_COMMAND} -E env CC=${CMAKE_C_COMPILER}
            bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/abi_layout.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/src ${R4L_RECOMP_UI_REFERENCE}/src)
endif()

# Parity: every launcher ABI member is used or documented as N/A (docs/PARITY.md),
# and docs/SUPPORTED.md matches the capability catalog.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME r4l_parity COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity_check.py ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME r4l_supported_doc COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_supported.py --check)
    # Every screen renders with every capability shown (hidden window, no audio).
    add_test(NAME r4l_render_all COMMAND r4l-skin-render --shots ${CMAKE_CURRENT_BINARY_DIR}/test-shots
             --assets ${CMAKE_CURRENT_SOURCE_DIR}/assets --skin default --size 1280x800)
    set_tests_properties(r4l_render_all PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy;R4L_SURFACE=all")
    # Stacked presets (r4 skin) must not pin the rows below them off screen.
    add_test(NAME r4l_graphics_reach COMMAND r4l-skin-render --run ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(r4l_graphics_reach PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;R4L_HIDDEN=1;R4L_SKIN=r4;R4L_SURFACE=all;R4L_SCREEN=Graphics;R4L_SIZE=1280x720;R4L_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/graphics_reach.txt"
        PASS_REGULAR_EXPRESSION "failures=0"
        FAIL_REGULAR_EXPRESSION "\\[r4l-script\\] FAIL")
    # The PC's hidden in-game window is 853x480.
    add_test(NAME r4l_graphics_reach_small COMMAND r4l-skin-render --run ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(r4l_graphics_reach_small PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;R4L_HIDDEN=1;R4L_SKIN=r4;R4L_SURFACE=all;R4L_SCREEN=Graphics;R4L_SIZE=853x480;R4L_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/graphics_reach.txt"
        PASS_REGULAR_EXPRESSION "failures=0"
        FAIL_REGULAR_EXPRESSION "\\[r4l-script\\] FAIL")
    # Right from any rail item enters the page (ImGui's own scoring needs a
    # vertically overlapping candidate, which a lower rail item has none of).
    add_test(NAME r4l_rail_right COMMAND r4l-skin-render --run ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(r4l_rail_right PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;R4L_HIDDEN=1;R4L_SKIN=r4;R4L_SURFACE=all;R4L_SCREEN=Graphics;R4L_SIZE=1280x720;R4L_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/rail_right.txt"
        PASS_REGULAR_EXPRESSION "failures=0"
        FAIL_REGULAR_EXPRESSION "\[r4l-script\] FAIL")
    # The Credits body scrolls under the wheel wherever the mouse is over it.
    add_test(NAME r4l_about_wheel COMMAND r4l-skin-render --run ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(r4l_about_wheel PROPERTIES
        ENVIRONMENT "SDL_AUDIODRIVER=dummy;R4L_HIDDEN=1;R4L_SKIN=r4;R4L_SURFACE=all;R4L_SCREEN=About;R4L_SIZE=1280x720;R4L_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/about_wheel.txt"
        PASS_REGULAR_EXPRESSION "failures=0"
        FAIL_REGULAR_EXPRESSION "\[r4l-script\] FAIL")
    # In-game menu main screen (launcher.main_screen): Start/Resume Game,
    # save states, Options with Back, Quit; first run with no disc: Select Disc.
    set(_r4l_menu_env "SDL_AUDIODRIVER=dummy;R4L_HIDDEN=1;R4L_SKIN=r4;R4L_SIZE=1280x720;R4L_NO_SOUND=1;R4L_NO_TRANSITION=1;R4L_NO_AUTOSCAN=1")
    foreach(_t menu_main_screen menu_states)
        add_test(NAME r4l_${_t} COMMAND r4l-skin-render --run-overlay ${CMAKE_CURRENT_SOURCE_DIR}/assets)
        set_tests_properties(r4l_${_t} PROPERTIES
            ENVIRONMENT "${_r4l_menu_env};R4L_FAKE_LOBBY=0;R4L_OVERLAY_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/${_t}.txt"
            PASS_REGULAR_EXPRESSION "failures=0"
            FAIL_REGULAR_EXPRESSION "\[r4l-script\] FAIL")
    endforeach()
    add_test(NAME r4l_menu_states_netplay COMMAND r4l-skin-render --run-overlay ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(r4l_menu_states_netplay PROPERTIES
        ENVIRONMENT "${_r4l_menu_env};R4L_FAKE_LOBBY=1;R4L_OVERLAY_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/menu_states_netplay.txt"
        PASS_REGULAR_EXPRESSION "failures=0"
        FAIL_REGULAR_EXPRESSION "\[r4l-script\] FAIL")
    add_test(NAME r4l_menu_no_disc COMMAND r4l-skin-render --run ${CMAKE_CURRENT_SOURCE_DIR}/assets)
    set_tests_properties(r4l_menu_no_disc PROPERTIES
        ENVIRONMENT "${_r4l_menu_env};R4L_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/menu_no_disc.txt"
        PASS_REGULAR_EXPRESSION "failures=0"
        FAIL_REGULAR_EXPRESSION "\[r4l-script\] FAIL")
endif()
