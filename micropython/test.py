import time
import struct

from pimoroni_i2c import PimoroniI2C
from machine import Pin

import ram

i2c = PimoroniI2C(sda=6, scl=7)
vsync = Pin(16, mode=Pin.IN)

time.sleep(0.5)

while 13 not in i2c.scan():
    time.sleep(0.5)

# SPI mode
i2c.writeto_mem(0x0D, 0xFE, bytearray((1,)))

# Init both RAM banks
ram.sel(0)
ram.reset()
ram.sel(1)
ram.reset()

# Write header
FRAME_WIDTH = 640
FRAME_HEIGHT = 480
def write_header(bank):
    ram.sel(bank)
    data = b"PICO" + bytes((1,1,1,1,
                            0,0,FRAME_WIDTH & 0xFF,FRAME_WIDTH >> 8,
                            0,0,FRAME_HEIGHT & 0xFF,FRAME_HEIGHT >> 8,
                            1,0,0,0,
                            FRAME_HEIGHT & 0xFF,FRAME_HEIGHT >> 8,1,bank,
                            0,0,0,0))
    ram.write(0, data)
    
write_header(0)
write_header(1)

# Write frame table for simple frame
BASE_DATA_ADDR = 0x10000
def write_frame_table():
    data_addr = BASE_DATA_ADDR
    addr = 28
    for i in range(FRAME_HEIGHT):
        data = 0x91000000 + data_addr
        ram.write(addr, struct.pack("<I", data))
        addr += 4
        data_addr += FRAME_WIDTH

ram.sel(0)
write_frame_table()
ram.sel(1)
write_frame_table()

# Simple fill
def fill_colour(val):
    addr = BASE_DATA_ADDR
    for y in range(FRAME_HEIGHT):
        for x in range(0, FRAME_WIDTH, 16):
            ram.write(addr, struct.pack("<H", val) * 16)
            addr += 32

ram.sel(0)
fill_colour(0x001F)
ram.sel(1)
fill_colour(0x7C00)

i2c.writeto_mem(0x0D, 0xF9, bytearray((1,)))

for i in range(320):
    while vsync.value() == 1: pass
    while vsync.value() == 0: pass

ram.sel(0)
