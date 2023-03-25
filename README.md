# Pico Stick Driver <!-- omit in toc -->

Pico Stick is a concept to have a system with 2 RP2040s, one driving the digital video and the other running application code.

This is the repo for driver for the "driver" side of the Pico stick, which uses PicoDVI to drive the display.

## Debugging notes

Command for launching OpenOCD on my laptop:

    C:\Users\mike\pico\openocd-x64-standalone\scripts> ..\openocd.exe -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000;bindto 0.0.0.0"