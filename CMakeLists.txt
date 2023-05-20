cmake_minimum_required(VERSION 3.12)

# Change your executable name to something creative!
set(NAME pico-stick) # <-- Name your project/executable here!
set(NAME_WIDE pico-stick-wide) # <-- Name your project/executable here!

#include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_NO_FLASH 1)
#set(PICO_COPY_TO_RAM 1)

# Initialize the SDK
pico_sdk_init()

# Enforce consistent compile options.
# For the moment, don't use -O3 options that increase code size significantly
add_compile_options(-Wall -Werror -O2 -fgcse-after-reload -floop-interchange -fpeel-loops -fpredictive-commoning -fsplit-paths -ftree-loop-distribute-patterns -ftree-loop-distribution -ftree-vectorize -ftree-partial-pre -funswitch-loops)


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
    edid.cpp
)

add_executable(${NAME_WIDE}
    main.cpp # <-- Add source files here!
    aps6404.cpp
    display.cpp
    frame_decode.cpp
    sprite.cpp
    i2c_interface.cpp
    edid.cpp
)

target_compile_definitions(${NAME} PRIVATE
  DVI_VERTICAL_REPEAT=1
  DVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg
  PICO_STACK_SIZE=0x300
  PICO_CORE1_STACK_SIZE=0x200
  DVI_N_TMDS_BUFFERS=0
  DVI_SYMBOLS_PER_WORD=2
  DVI_DEFAULT_PIO_INST=pio1
  TMDS_FULLRES_NO_INTERP_SAVE=1
  PICO_HEAP_SIZE=2048
  )

target_compile_definitions(${NAME_WIDE} PRIVATE
  DVI_VERTICAL_REPEAT=1
  DVI_DEFAULT_SERIAL_CONFIG=pico_sock_cfg
  PICO_STACK_SIZE=0x300
  PICO_CORE1_STACK_SIZE=0x200
  DVI_N_TMDS_BUFFERS=0
  DVI_SYMBOLS_PER_WORD=2
  DVI_DEFAULT_PIO_INST=pio1
  TMDS_FULLRES_NO_INTERP_SAVE=1
  PICO_HEAP_SIZE=2048
  SUPPORT_WIDE_MODES=1
  )

set_target_properties(${NAME} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap.ld)
pico_add_link_depend(${NAME} ${CMAKE_CURRENT_LIST_DIR}/memmap.ld)
pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/aps6404.pio)

set_target_properties(${NAME_WIDE} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap.ld)
pico_add_link_depend(${NAME_WIDE} ${CMAKE_CURRENT_LIST_DIR}/memmap.ld)
pico_generate_pio_header(${NAME_WIDE} ${CMAKE_CURRENT_LIST_DIR}/aps6404.pio)

# Don't forget to link the libraries you need!
target_link_libraries(${NAME} pico_stdlib pico_multicore i2c_slave libdvi hardware_watchdog)
target_link_libraries(${NAME_WIDE} pico_stdlib pico_multicore i2c_slave libdvi hardware_watchdog)

# create map/bin/hex file etc.
#pico_add_extra_outputs(${NAME})

pico_enable_stdio_usb(${NAME} 0)
pico_enable_stdio_uart(${NAME} 1)
pico_enable_stdio_usb(${NAME_WIDE} 0)
pico_enable_stdio_uart(${NAME_WIDE} 1)

add_custom_command(TARGET ${NAME} POST_BUILD
    COMMAND ${CMAKE_OBJDUMP} -s $<TARGET_FILE:${NAME}> >$<IF:$<BOOL:$<TARGET_PROPERTY:${NAME},OUTPUT_NAME>>,$<TARGET_PROPERTY:${NAME},OUTPUT_NAME>,$<TARGET_PROPERTY:${NAME},NAME>>.dmp
    COMMAND python ${CMAKE_CURRENT_LIST_DIR}/convert_elf.py $<IF:$<BOOL:$<TARGET_PROPERTY:${NAME},OUTPUT_NAME>>,$<TARGET_PROPERTY:${NAME},OUTPUT_NAME>,$<TARGET_PROPERTY:${NAME},NAME>>.dmp >$<IF:$<BOOL:$<TARGET_PROPERTY:${NAME},OUTPUT_NAME>>,$<TARGET_PROPERTY:${NAME},OUTPUT_NAME>,$<TARGET_PROPERTY:${NAME},NAME>>.h
)
add_custom_command(TARGET ${NAME_WIDE} POST_BUILD
    COMMAND ${CMAKE_OBJDUMP} -s $<TARGET_FILE:${NAME_WIDE}> >$<IF:$<BOOL:$<TARGET_PROPERTY:${NAME_WIDE},OUTPUT_NAME>>,$<TARGET_PROPERTY:${NAME_WIDE},OUTPUT_NAME>,$<TARGET_PROPERTY:${NAME_WIDE},NAME>>.dmp
    COMMAND python ${CMAKE_CURRENT_LIST_DIR}/convert_elf.py $<IF:$<BOOL:$<TARGET_PROPERTY:${NAME_WIDE},OUTPUT_NAME>>,$<TARGET_PROPERTY:${NAME_WIDE},OUTPUT_NAME>,$<TARGET_PROPERTY:${NAME_WIDE},NAME>>.dmp >$<IF:$<BOOL:$<TARGET_PROPERTY:${NAME_WIDE},OUTPUT_NAME>>,$<TARGET_PROPERTY:${NAME_WIDE},OUTPUT_NAME>,$<TARGET_PROPERTY:${NAME_WIDE},NAME>>.h
)

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.elf
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.h
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME_WIDE}.elf
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME_WIDE}.h
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
