# DV Stick Driver <!-- omit in toc -->

The DV Stick is a system with 2 RP2040s, one driving the digital video (the GPU) and the other running application code (the CPU).

This is the repo for the GPU side of the DV stick, which uses PicoDVI to drive the display.

## Building

Note that currently this firmware only builds using gcc 9.

## Getting it running

The driver RP2040 has no flash and is designed to be programmed over SWD either from the debugging port, or direct from the application CPU Pico.

## Loading from the CPU

The build produces a `pico-stick.h` in the `build/` directory that should be copied to `drivers/dv_display` in the [dv_stick branch](https://github.com/MichaelBell/pimoroni-pico/tree/dv_stick) of the pimoroni-pico repo.  This is also saved in the github action artifact.

When the DVDisplay driver is initialized it will upload the image from `pico-stick.h` to the driver RP2040.

## Writing your own CPU side firmware

If you want to bypass the `dv_display` and write your own CPU side firmware, this describes the [format for the data read from RAM](https://github.com/MichaelBell/pico-stick/blob/main/FrameFormat.txt) by the GPU.  I would recommend grabbing the SWD programmer from the dv_display driver to get the GPU firmware loaded.

Between RAM bank switches the CPU interacts with the GPU over I2C, the interface is [documented in a spreadsheet](https://docs.google.com/spreadsheets/d/1PKt1zPrB67C1ntRw4sIHiO5FZF0tHdjhlcEdujFQAuE/edit#gid=0).

## Loading over SWD for debugging

You will need an SWD connection to the debugging port on the DV stick - this is connected to the driver RP2040.  If you're on Windows the easiest way is with a RPi Debug Probe, or if you're using a Raspberry Pi you can wire it up to the SWD as normal.

You'll need to use a customised version of the OpenOCD rp2040.cfg file, because there is no flash attached, and that is assumed by the default config.  Copy the file from `openocd/target/` in this repo to scripts/target in your OpenOCD install.

This is the command for launching OpenOCD on my laptop, you should be able to adapt accordingly:

    C:\Users\mike\pico\openocd-x64-standalone\scripts> ..\openocd.exe -f interface/cmsis-dap.cfg -f target/rp2040-no-flash.cfg -c "adapter speed 5000;bindto 0.0.0.0"

Once you have OpenOCD running you should be able to load the project in VS code and launch it in the debugger - after setting an appropriate IP for OpenOCD in `launch.json` (if required).

Alternatively, you should be able to grab the elf from the github build action and program with a command along the lines of:

    gdb-multiarch -ex "target remote localhost:3333" -ex "monitor reset init" -ex "load" -ex "continue" pico-stick.elf

If you're using an application that normally loads the driver itself,don't forget to comment out `swd_load_program` in `dv_display.cpp`.

## Credits

This driver would not be possible without [PicoDVI](https://github.com/Wren6991/PicoDVI) by Luke Wren - this repo is just a wrapper around PicoDVI.  The version used here has several modifications to increase speed at the cost of using more RAM - which for this system is a better tradeoff as the application is running on a different processor.

The [i2c slave library](https://github.com/vmilea/pico_i2c_slave/) is by Valentin Milea.