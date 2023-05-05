# DV Stick Driver <!-- omit in toc -->

The DV Stick is a system with 2 RP2040s, one driving the digital video and the other running application code.

This is the repo for the "driver" side of the DV stick, which uses PicoDVI to drive the display.

## Getting it running

The driver RP2040 has no flash and is designed to be programmed over SWD either from the debugging port, or direct from the application Pico.

For now, you will need an SWD connection to the debugging port.  If you're on Windows the easiest way is with a RPi Debug Probe, or if you're using a Raspberry Pi you can wire it up to the SWD as normal.

You'll need to use a customised version of the OpenOCD rp2040.cfg file, becuse there is no flash attached, and that is assumed by the default config.  Copy the file from `openocd/target/` in this repo to scripts/target in your OpenOCD install.

This is the command for launching OpenOCD on my laptop, you should be able to adapt accordingly:

    C:\Users\mike\pico\openocd-x64-standalone\scripts> ..\openocd.exe -f interface/cmsis-dap.cfg -f target/rp2040-no-flash.cfg -c "adapter speed 5000;bindto 0.0.0.0"

Once you have OpenOCD running you should be able to load the project in VS code and launch it in the debugger - after setting an appropriate IP for OpenOCD in `launch.json` (if required).

Alternatively, you should be able to grab the elf from the github build action and program with a command along the lines of:

    gdb-multiarch -ex "target remote localhost:3333" -ex "monitor reset init" -ex "load" -ex "continue" pico-stick.elf
