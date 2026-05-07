cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME L496_0405)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Enable dead code stripping for size optimization
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# LVGL core source files
file(GLOB_RECURSE LVGL_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/core/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/hal/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/misc/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/draw/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/draw/sw/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/font/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/widgets/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/extra/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/extra/themes/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/extra/libs/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/extra/libs/fsdrv/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/extra/libs/png/*.c
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src/extra/libs/bmp/*.c
)

# Exclude platform-specific drivers that are not needed
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/draw/sdl/.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/draw/nxp/.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/draw/renesas/.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/draw/arm2d/.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/draw/stm32_dma2d/.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/draw/swm341_dma2d/.*")
# Exclude extra components that may not be needed
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/extra/libs/rlottie/.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/extra/libs/.*win32.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/extra/libs/.*stdio.*")
list(FILTER LVGL_CORE_SOURCES EXCLUDE REGEX ".*/extra/libs/.*posix.*")

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    # USER CODE BEGIN SOURCES
    ${CMAKE_SOURCE_DIR}/User/LCD/LCD_1in69.c
    ${CMAKE_SOURCE_DIR}/User/touch.c
    ${CMAKE_SOURCE_DIR}/User/Config/usb_printf.c
    ${CMAKE_SOURCE_DIR}/Core/Src/max3003.c
    ${CMAKE_SOURCE_DIR}/Core/Src/ecg_record_control.c
    ${CMAKE_SOURCE_DIR}/Core/Src/ecg_sd_logger.c
    ${CMAKE_SOURCE_DIR}/App/LVGL/app_lvgl.c
    ${CMAKE_SOURCE_DIR}/App/LVGL/lv_port_disp.c
    ${CMAKE_SOURCE_DIR}/App/LVGL/lv_port_indev.c
    ${CMAKE_SOURCE_DIR}/User/Config/DEV_Config.c
    ${CMAKE_SOURCE_DIR}/User/Fonts/font12.c
    ${CMAKE_SOURCE_DIR}/User/Fonts/font12CN.c
    ${CMAKE_SOURCE_DIR}/User/Fonts/font16.c
    ${CMAKE_SOURCE_DIR}/User/Fonts/font20.c
    ${CMAKE_SOURCE_DIR}/User/Fonts/font24.c
    ${CMAKE_SOURCE_DIR}/User/Fonts/font24CN.c
    ${CMAKE_SOURCE_DIR}/User/Fonts/font8.c
    ${CMAKE_SOURCE_DIR}/User/image/image.c
    ${LVGL_CORE_SOURCES}
    # USER CODE END SOURCES
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    # USER CODE BEGIN INCLUDES
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/User/Config
    ${CMAKE_SOURCE_DIR}/User/example
    ${CMAKE_SOURCE_DIR}/User
    ${CMAKE_SOURCE_DIR}/User/LCD
    ${CMAKE_SOURCE_DIR}/User/GUI_DEV
    ${CMAKE_SOURCE_DIR}/User/Fonts
    ${CMAKE_SOURCE_DIR}/User/image
    ${CMAKE_SOURCE_DIR}/App/LVGL
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/LVGL/src
    # USER CODE END INCLUDES
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
