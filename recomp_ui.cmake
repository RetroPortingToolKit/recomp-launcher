# recomp_ui.cmake — drop-in integration for psxrecomp games.
#
# A game points psxrecomp at this repo instead of recomp-ui:
#     -DRECOMP_UI_ROOT=<path-to-recomp-launcher>
# (or checks it out as the game's root `recomp-ui` submodule path). psxrecomp's
# runtime.cmake then include()s this file and calls
#     recomp_target_launcher_ui(<runtime-target> CONSOLE psx [BOXART f] [PAD f] [BRAND f])
# exactly as it does for recomp-ui. See docs/DESIGN.md, "CMake integration".

include_guard(GLOBAL)

set(RECOMP_UI_ROOT "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "Path to the launcher" FORCE)
set(R4L_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(R4L_VERSION "0.1.0")
option(RECOMP_UI_ENABLE_MODS "Show the Mods screen" ON)
option(RECOMP_UI_SDL3 "SDL3 platform backend (the only one this launcher has)" ON)
set(R4L_TITLE "r4" CACHE STRING "Title layer under titles/<id>/")
# A game repo may own its title layer (manifest, art extraction, credits):
# set R4L_TITLE_SOURCE to its .cpp before psxrecomp includes this file.
if(NOT R4L_TITLE_SOURCE)
    set(R4L_TITLE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/titles/${R4L_TITLE}/title_${R4L_TITLE}.cpp")
endif()
set(R4L_UI_LANGUAGE "" CACHE STRING "Launcher UI language (assets/i18n/<code>.json), empty = English")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/recomp_gl.cmake")

set(R4L_IMGUI "${R4L_ROOT}/third_party/imgui")
if(NOT EXISTS "${R4L_IMGUI}/imgui.cpp")
    message(FATAL_ERROR "recomp-launcher: Dear ImGui is missing. Run: "
        "git -C ${R4L_ROOT} submodule update --init third_party/imgui")
endif()

set(R4L_SOURCES
    ${R4L_ROOT}/src/r4l/abi_entry.cpp
    ${R4L_ROOT}/src/r4l/boot_timing.c
    ${R4L_ROOT}/src/r4l/overlay_entry.cpp
    ${R4L_ROOT}/src/r4l/psx_menu_provider.cpp
    ${R4L_ROOT}/src/common/recomp_runtime_ui.c  # recomp-ui 6771ea0: psxrecomp psx_runtime_menu.c fallback
    ${R4L_ROOT}/src/r4l/core/binds.cpp
    ${R4L_ROOT}/src/r4l/core/discfs.cpp
    ${R4L_ROOT}/src/r4l/core/discscan.cpp
    ${R4L_ROOT}/src/r4l/core/png.cpp
    ${R4L_ROOT}/src/r4l/core/memcard.cpp
    ${R4L_ROOT}/src/r4l/core/surface.cpp
    ${R4L_ROOT}/src/r4l/core/json.cpp
    ${R4L_ROOT}/src/r4l/core/skin_model.cpp
    ${R4L_ROOT}/src/r4l/core/ini.cpp
    ${R4L_ROOT}/src/r4l/core/mods.cpp
    ${R4L_ROOT}/src/r4l/core/quality.cpp
    ${R4L_ROOT}/src/r4l/core/session.cpp
    ${R4L_ROOT}/src/r4l/core/steam.cpp
    ${R4L_ROOT}/src/r4l/ui/app.cpp
    ${R4L_ROOT}/src/r4l/ui/layout.cpp
    ${R4L_ROOT}/src/r4l/ui/skin.cpp
    ${R4L_ROOT}/src/r4l/ui/platform.cpp
    ${R4L_ROOT}/src/r4l/ui/screen_netplay.cpp
    ${R4L_ROOT}/src/r4l/ui/screen_netplay_more.cpp
    ${R4L_ROOT}/src/r4l/ui/screens_system.cpp
    ${R4L_ROOT}/src/r4l/ui/script.cpp
    ${R4L_ROOT}/src/r4l/ui/screens_settings.cpp
    ${R4L_TITLE_SOURCE}
    ${R4L_IMGUI}/imgui.cpp
    ${R4L_IMGUI}/imgui_draw.cpp
    ${R4L_IMGUI}/imgui_tables.cpp
    ${R4L_IMGUI}/imgui_widgets.cpp
    ${R4L_IMGUI}/backends/imgui_impl_sdl3.cpp
    ${R4L_IMGUI}/backends/imgui_impl_opengl3.cpp)

# Compile the launcher into <TGT> (same model as recomp-ui: sources join the
# host target, which already links SDL3 and GL).
function(recomp_target_launcher_ui TGT)
    cmake_parse_arguments(RUI "HOST_IMGUI" "CONSOLE;BOXART;BOXART_NAME;PAD;BRAND;IMGUI_DIR;LANGUAGE" "" ${ARGN})
    if(NOT RECOMP_UI_SDL3)
        message(FATAL_ERROR "recomp-launcher supports SDL3 only")
    endif()
    if(RUI_HOST_IMGUI)
        message(FATAL_ERROR "recomp-launcher: HOST_IMGUI is not supported yet")
    endif()
    set_target_properties(${TGT} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_sources(${TGT} PRIVATE ${R4L_SOURCES})
    target_include_directories(${TGT} PRIVATE ${R4L_ROOT}/src ${R4L_ROOT} ${R4L_IMGUI} ${R4L_IMGUI}/backends ${R4L_ROOT}/third_party/stb)
    target_compile_definitions(${TGT} PRIVATE
        RECOMP_LAUNCHER
        IMGUI_ENABLE_TEST_ENGINE  # item hooks for R4L_SCRIPT (script.cpp); inert unless scripting
        RECOMP_UI_ENABLE_MODS=$<BOOL:${RECOMP_UI_ENABLE_MODS}>
        SDL_MAIN_HANDLED
        R4L_VERSION="${R4L_VERSION}"
        R4L_DEFAULT_LANGUAGE="${R4L_UI_LANGUAGE}")
    find_package(Threads REQUIRED)
    target_link_libraries(${TGT} PRIVATE Threads::Threads)
    if(NOT WIN32)
        recomp_resolve_gl(_r4l_gl)
        target_link_libraries(${TGT} PRIVATE ${_r4l_gl} ${CMAKE_DL_LIBS})
    else()
        target_link_libraries(${TGT} PRIVATE opengl32)
    endif()
    recomp_stage_launcher_assets(${TGT} CONSOLE "${RUI_CONSOLE}" BOXART "${RUI_BOXART}" BOXART_NAME "${RUI_BOXART_NAME}")
endfunction()

# Stage fonts (and box art) to <exe-dir>/assets.
function(recomp_stage_launcher_assets TGT)
    cmake_parse_arguments(RSA "" "CONSOLE;BOXART;BOXART_NAME" "" ${ARGN})
    add_custom_command(TARGET ${TGT} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TGT}>/assets/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${R4L_ROOT}/assets/fonts/LatoLatin-Regular.ttf"
            "${R4L_ROOT}/assets/fonts/LatoLatin-Bold.ttf"
            "${R4L_ROOT}/assets/fonts/NOTICE.md"
            "$<TARGET_FILE_DIR:${TGT}>/assets/fonts/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${R4L_ROOT}/assets/skins"
            "$<TARGET_FILE_DIR:${TGT}>/assets/skins"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${R4L_ROOT}/assets/i18n"
            "$<TARGET_FILE_DIR:${TGT}>/assets/i18n"
        # assets/img always exists: release packagers stage its NOTICE.md
        # (assets/common/img/NOTICE.md) there.
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TGT}>/assets/img"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${R4L_ROOT}/assets/common/img/NOTICE.md"
            "$<TARGET_FILE_DIR:${TGT}>/assets/img/NOTICE.md"
        COMMENT "Staging launcher fonts and skins" VERBATIM)
    if(RSA_BOXART AND EXISTS "${RSA_BOXART}")
        set(_dest boxart.tga)
        if(RSA_BOXART_NAME)
            set(_dest ${RSA_BOXART_NAME})
        endif()
        add_custom_command(TARGET ${TGT} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TGT}>/assets/img"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RSA_BOXART}"
                "$<TARGET_FILE_DIR:${TGT}>/assets/img/${_dest}" VERBATIM)
    endif()
endfunction()
