pico_sdk_init()

include_directories(${CMAKE_CURRENT_LIST_DIR}
                    ${CMAKE_CURRENT_LIST_DIR}/FreeRTOSConfig)
add_compile_options(-Wall -Werror -Wfatal-errors -Wno-error=unused-variable -Og -g)

function(make_firmware board board_def)
    add_executable(${board}
        main.c button.c rgb.c save.c config.c cli.c commands.c io.c hid.c
        uart.c
        # touch.c mpr121.c
        usb_descriptors.c)
    target_compile_definitions(${board} PUBLIC ${board_def})
    pico_enable_stdio_usb(${board} 1)
    pico_enable_stdio_uart(${board} 0)

    pico_generate_pio_header(${board} ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

    
    add_compile_definitions(AZAMAI_BUILD)

    target_link_libraries(${board} PRIVATE
        aic
        pico_multicore pico_stdlib hardware_pio hardware_pwm hardware_flash
        hardware_adc hardware_i2c hardware_watchdog pico_unique_id
        tinyusb_device tinyusb_board
        FreeRTOS-Kernel FreeRTOS-Kernel-Heap4)

    pico_add_extra_outputs(${board})

    add_custom_command(TARGET ${board} PRE_BUILD
    COMMAND touch ${CMAKE_CURRENT_SOURCE_DIR}/cli.c)

    add_custom_command(TARGET ${board} POST_BUILD
                       COMMAND cp ${board}.uf2 ${CMAKE_CURRENT_LIST_DIR}/..)
endfunction()

make_firmware(mai_pico BOARD_MAI_PICO)

