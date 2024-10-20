cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must set be before project)
include(pico_sdk_import.cmake)

set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/modules/freertos)
include(FreeRTOS_Kernel_import.cmake)

project(mai_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_subdirectory(modules/aic_pico/firmware aic)
include_directories(modules/aic_pico/firmware/include)

add_subdirectory(src)
