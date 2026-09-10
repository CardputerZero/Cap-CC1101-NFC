cmake_minimum_required(VERSION 3.16)

project(M5CardputerZeroCapCC1101NFC VERSION 0.1.3 LANGUAGES C CXX ASM)

option(CAP_NFC_USE_SDL "Use LVGL's SDL backend and mock NFC reader" ON)
option(BUILD_TESTING "Build Cap-CC1101-NFC tests" OFF)
set(CAP_NFC_BIN_NAME "M5CardputerZero-Cap-CC1101-NFC" CACHE STRING "Output executable name")
set(CAP_NFC_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/dependencies" CACHE PATH "Third-party source directory")
if(CAP_NFC_USE_SDL)
    set(CAP_NFC_DEFAULT_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/dist/sdl")
else()
    set(CAP_NFC_DEFAULT_OUTPUT_DIR "${CMAKE_CURRENT_LIST_DIR}/dist/device")
endif()

set(CAP_NFC_OUTPUT_DIR_IS_DEFAULT OFF)
if(NOT DEFINED CAP_NFC_OUTPUT_DIR)
    set(CAP_NFC_OUTPUT_DIR_IS_DEFAULT ON)
elseif(DEFINED CAP_NFC_LAST_DEFAULT_OUTPUT_DIR AND
       "${CAP_NFC_OUTPUT_DIR}" STREQUAL "${CAP_NFC_LAST_DEFAULT_OUTPUT_DIR}")
    set(CAP_NFC_OUTPUT_DIR_IS_DEFAULT ON)
endif()
if(CAP_NFC_OUTPUT_DIR_IS_DEFAULT)
    set(CAP_NFC_OUTPUT_DIR "${CAP_NFC_DEFAULT_OUTPUT_DIR}" CACHE PATH "Runtime output directory" FORCE)
endif()
set(CAP_NFC_LAST_DEFAULT_OUTPUT_DIR "${CAP_NFC_DEFAULT_OUTPUT_DIR}" CACHE INTERNAL
    "Last automatically selected runtime output directory" FORCE)

include(CTest)

if(APPLE AND NOT CAP_NFC_USE_SDL)
    message(FATAL_ERROR "macOS builds only support CAP_NFC_USE_SDL=ON.")
endif()
if(NOT CAP_NFC_USE_SDL AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The ST25R3916 hardware backend requires Linux.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CAP_NFC_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(CAP_NFC_SRC_DIR "${CAP_NFC_ROOT}/src")
set(CAP_NFC_LVGL_DIR "${CAP_NFC_DEPS_DIR}/lvgl")
set(CAP_NFC_SPDLOG_DIR "${CAP_NFC_DEPS_DIR}/spdlog")
set(CAP_NFC_SMOOTH_UI_DIR "${CAP_NFC_DEPS_DIR}/smooth_ui_toolkit")
set(CAP_NFC_SYSTEM_LIBS ${CMAKE_DL_LIBS})

if(UNIX AND NOT APPLE)
    list(APPEND CAP_NFC_SYSTEM_LIBS m)
endif()

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

foreach(required_dir IN LISTS CAP_NFC_LVGL_DIR CAP_NFC_SPDLOG_DIR CAP_NFC_SMOOTH_UI_DIR)
    if(NOT EXISTS "${required_dir}/CMakeLists.txt")
        message(FATAL_ERROR "Missing dependency: ${required_dir}. Run ./bootstrap.sh first or set CAP_NFC_DEPS_DIR.")
    endif()
endforeach()

find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

set(LV_BUILD_CONF_PATH "${CAP_NFC_ROOT}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)

set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF CACHE BOOL "" FORCE)

add_subdirectory("${CAP_NFC_LVGL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/lvgl" EXCLUDE_FROM_ALL)
add_subdirectory("${CAP_NFC_SPDLOG_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/spdlog" EXCLUDE_FROM_ALL)
add_subdirectory("${CAP_NFC_SMOOTH_UI_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/dependencies/smooth_ui_toolkit"
                 EXCLUDE_FROM_ALL)

function(cap_nfc_add_sdl2_parent_include target imported_target)
    if(NOT TARGET "${imported_target}")
        return()
    endif()

    get_target_property(sdl2_include_dirs "${imported_target}" INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT sdl2_include_dirs)
        return()
    endif()

    foreach(sdl2_include_dir IN LISTS sdl2_include_dirs)
        if(sdl2_include_dir MATCHES "/SDL2$")
            get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
            target_include_directories("${target}" SYSTEM PRIVATE "${sdl2_parent_include_dir}")
        endif()
    endforeach()
endfunction()

set(CAP_NFC_LVGL_POSIX_PATH "./")
if(NOT CAP_NFC_USE_SDL)
    set(CAP_NFC_LVGL_POSIX_PATH "/usr/share/Cap-CC1101-NFC/")
endif()

target_compile_definitions(lvgl PUBLIC
    CAP_NFC_LVGL_USE_SDL=$<IF:$<BOOL:${CAP_NFC_USE_SDL}>,1,0>
    CAP_NFC_LVGL_FS_POSIX_PATH="${CAP_NFC_LVGL_POSIX_PATH}"
)

if(CAP_NFC_USE_SDL)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()

    if(SDL2_FOUND)
        target_link_libraries(lvgl PRIVATE PkgConfig::SDL2)
        cap_nfc_add_sdl2_parent_include(lvgl PkgConfig::SDL2)
        set(CAP_NFC_SDL_LIBS PkgConfig::SDL2)
    else()
        find_package(SDL2 CONFIG QUIET)
        if(TARGET SDL2::SDL2)
            target_link_libraries(lvgl PRIVATE SDL2::SDL2)
            cap_nfc_add_sdl2_parent_include(lvgl SDL2::SDL2)
            set(CAP_NFC_SDL_LIBS SDL2::SDL2)
        else()
            find_package(SDL2 REQUIRED)
            target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
            foreach(sdl2_include_dir IN LISTS SDL2_INCLUDE_DIRS)
                if(sdl2_include_dir MATCHES "/SDL2$")
                    get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
                    target_include_directories(lvgl SYSTEM PRIVATE "${sdl2_parent_include_dir}")
                endif()
            endforeach()
            set(CAP_NFC_SDL_LIBS ${SDL2_LIBRARIES})
        endif()
    endif()
endif()

file(GLOB CAP_NFC_APP_CPP CONFIGURE_DEPENDS
    "${CAP_NFC_SRC_DIR}/main.cpp"
    "${CAP_NFC_SRC_DIR}/core/*.cpp"
    "${CAP_NFC_SRC_DIR}/input/*.cpp"
    "${CAP_NFC_SRC_DIR}/models/*.cpp"
    "${CAP_NFC_SRC_DIR}/view_models/*.cpp"
    "${CAP_NFC_SRC_DIR}/views/*.cpp"
)
set(CAP_NFC_HAL_CPP "${CAP_NFC_SRC_DIR}/hal/nfc_lvgl_hal.cpp")

set(CAP_NFC_DOMAIN_CPP
    "${CAP_NFC_SRC_DIR}/nfc/nfc_backend.cpp"
    "${CAP_NFC_SRC_DIR}/nfc/ndef_parser.cpp"
    "${CAP_NFC_SRC_DIR}/nfc/nfc_types.cpp"
    "${CAP_NFC_SRC_DIR}/nfc/nfc_worker.cpp"
)

if(CAP_NFC_USE_SDL)
    list(APPEND CAP_NFC_DOMAIN_CPP "${CAP_NFC_SRC_DIR}/nfc/mock_nfc_backend.cpp")
else()
    if(PkgConfig_FOUND)
        pkg_check_modules(GPIOD QUIET IMPORTED_TARGET libgpiod)
    endif()

    if(GPIOD_FOUND)
        set(CAP_NFC_GPIOD_TARGET PkgConfig::GPIOD)
    else()
        find_path(GPIOD_INCLUDE_DIR NAMES gpiod.h)
        find_library(GPIOD_LIBRARY NAMES gpiod libgpiod.so.3.1.1 libgpiod.so.3 libgpiod.so.2)
        if(NOT GPIOD_INCLUDE_DIR OR NOT GPIOD_LIBRARY)
            message(FATAL_ERROR
                    "libgpiod development files were not found. Install libgpiod-dev or provide them in the target sysroot.")
        endif()

        add_library(cap_nfc_gpiod UNKNOWN IMPORTED)
        set_target_properties(cap_nfc_gpiod PROPERTIES
            IMPORTED_LOCATION "${GPIOD_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GPIOD_INCLUDE_DIR}"
        )
        set(CAP_NFC_GPIOD_TARGET cap_nfc_gpiod)
        set(GPIOD_INCLUDE_DIRS "${GPIOD_INCLUDE_DIR}")
    endif()

    set(CAP_NFC_GPIOD_V2 OFF)
    if(GPIOD_VERSION VERSION_GREATER_EQUAL "2.0")
        set(CAP_NFC_GPIOD_V2 ON)
    else()
        foreach(gpiod_include_dir IN LISTS GPIOD_INCLUDE_DIRS)
            if(EXISTS "${gpiod_include_dir}/gpiod.h")
                file(STRINGS "${gpiod_include_dir}/gpiod.h" GPIOD_V2_DECLARATION
                     REGEX "gpiod_line_settings_new" LIMIT_COUNT 1)
                if(GPIOD_V2_DECLARATION)
                    set(CAP_NFC_GPIOD_V2 ON)
                endif()
                break()
            endif()
        endforeach()
    endif()

    list(APPEND CAP_NFC_DOMAIN_CPP
        "${CAP_NFC_SRC_DIR}/nfc/linux_nfc_backend.cpp"
        "${CAP_NFC_SRC_DIR}/nfc/st25r3916_driver.cpp"
    )
    list(APPEND CAP_NFC_HAL_CPP
        "${CAP_NFC_SRC_DIR}/hal/cap_spi_overlay.cpp"
        "${CAP_NFC_SRC_DIR}/hal/cardputerzero_cap_power.cpp"
        "${CAP_NFC_SRC_DIR}/hal/linux_gpio_line.cpp"
        "${CAP_NFC_SRC_DIR}/hal/linux_spi_device.cpp"
    )
endif()

add_executable(${CAP_NFC_BIN_NAME}
    ${CAP_NFC_APP_CPP}
    ${CAP_NFC_HAL_CPP}
    ${CAP_NFC_DOMAIN_CPP}
)

target_include_directories(${CAP_NFC_BIN_NAME} PRIVATE "${CAP_NFC_SRC_DIR}")
target_compile_definitions(${CAP_NFC_BIN_NAME} PRIVATE
    CAP_NFC_USE_MOCK_BACKEND=$<IF:$<BOOL:${CAP_NFC_USE_SDL}>,1,0>
    CAP_NFC_USE_LINUX_BACKEND=$<IF:$<BOOL:${CAP_NFC_USE_SDL}>,0,1>
    CAP_NFC_USE_UNAVAILABLE_BACKEND=0
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${CAP_NFC_BIN_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(NOT CAP_NFC_USE_SDL AND CAP_NFC_GPIOD_V2)
    target_compile_definitions(${CAP_NFC_BIN_NAME} PRIVATE GPIOD_V2=1)
endif()

target_link_libraries(${CAP_NFC_BIN_NAME} PRIVATE
    lvgl::lvgl
    smooth_ui_toolkit
    spdlog::spdlog
    Threads::Threads
    ${CAP_NFC_SDL_LIBS}
    ${CAP_NFC_SYSTEM_LIBS}
)
if(NOT CAP_NFC_USE_SDL)
    target_link_libraries(${CAP_NFC_BIN_NAME} PRIVATE ${CAP_NFC_GPIOD_TARGET})
endif()

set_target_properties(${CAP_NFC_BIN_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CAP_NFC_OUTPUT_DIR}"
)

if(BUILD_TESTING)
    add_executable(cap_nfc_worker_tests
        tests/nfc_worker_test.cpp
        src/models/nfc_model.cpp
        src/nfc/nfc_backend.cpp
        src/nfc/nfc_types.cpp
        src/nfc/nfc_worker.cpp
        src/nfc/unavailable_nfc_backend.cpp
    )
    target_include_directories(cap_nfc_worker_tests PRIVATE "${CAP_NFC_SRC_DIR}")
    target_compile_definitions(cap_nfc_worker_tests PRIVATE CAP_NFC_USE_UNAVAILABLE_BACKEND=1)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cap_nfc_worker_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(cap_nfc_worker_tests PRIVATE smooth_ui_toolkit spdlog::spdlog Threads::Threads)
    add_test(NAME cap_nfc_worker COMMAND cap_nfc_worker_tests)

    add_executable(cap_nfc_ndef_parser_tests
        tests/ndef_parser_test.cpp
        src/nfc/ndef_parser.cpp
    )
    target_include_directories(cap_nfc_ndef_parser_tests PRIVATE "${CAP_NFC_SRC_DIR}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cap_nfc_ndef_parser_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME cap_nfc_ndef_parser COMMAND cap_nfc_ndef_parser_tests)

    add_executable(cap_nfc_st25r3916_driver_tests
        tests/st25r3916_driver_test.cpp
        src/nfc/nfc_types.cpp
        src/nfc/st25r3916_driver.cpp
    )
    target_include_directories(cap_nfc_st25r3916_driver_tests PRIVATE "${CAP_NFC_SRC_DIR}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cap_nfc_st25r3916_driver_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    target_link_libraries(cap_nfc_st25r3916_driver_tests PRIVATE spdlog::spdlog Threads::Threads)
    add_test(NAME cap_nfc_st25r3916_driver COMMAND cap_nfc_st25r3916_driver_tests)
endif()
