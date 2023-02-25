cmake_minimum_required(VERSION 3.12)

# Change your executable name to something creative!
set(NAME pico-stick) # <-- Name your project/executable here!

#include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_COPY_TO_RAM 1)

# Initialize the SDK
pico_sdk_init()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")

add_subdirectory(i2c_slave)
add_subdirectory(PicoDVI/software/libdvi)
include_directories(PicoDVI/software/include PicoDVI/software/assets)

# Add your source files
add_executable(${NAME}
    main.cpp # <-- Add source files here!
    aps6404.cpp
    display.cpp
    frame_decode.cpp
    sprite.cpp
    i2c_interface.cpp
)

target_compile_definitions(${NAME} PRIVATE
  DVI_VERTICAL_REPEAT=1
  DVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg
  PICO_STACK_SIZE=0x200
  PICO_CORE1_STACK_SIZE=0x180
  DVI_N_TMDS_BUFFERS=8
  DVI_SYMBOLS_PER_WORD=2
  DVI_DEFAULT_PIO_INST=pio1
  TMDS_FULLRES_NO_INTERP_SAVE=1
  PICO_DEFAULT_UART=1
  PICO_DEFAULT_UART_TX_PIN=20
  PICO_DEFAULT_UART_RX_PIN=21
  )

pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/aps6404.pio)

# Include required libraries
# This assumes `pimoroni-pico` is stored alongside your project
#include(common/pimoroni_i2c)

# Don't forget to link the libraries you need!
target_link_libraries(${NAME}
    pico_stdlib pico_multicore
    i2c_slave
    libdvi
    hardware_watchdog
#    pimoroni_i2c
)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

pico_enable_stdio_usb(${NAME} 0)
pico_enable_stdio_uart(${NAME} 1)

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
