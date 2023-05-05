from machine import SPI, Pin

sel = Pin(8, mode=Pin.OUT, value=0)
cs = Pin(17, mode=Pin.OUT, value=1)

# Bit bang leaving QPI mode
def leave_qpi():
    d0 = Pin(19, mode=Pin.OUT, value=1)
    d1 = Pin(20, mode=Pin.OUT, value=1)
    d2 = Pin(21, mode=Pin.OUT, value=1)
    d3 = Pin(22, mode=Pin.OUT, value=1)
    clk = Pin(18, mode=Pin.OUT, value=0)
    cs(0)
    clk(1)
    clk(0)
    d1(0)
    d3(0)
    clk(1)
    clk(0)
    cs(1)
    
    del d0, d1, d2, d3, clk

leave_qpi()
sel(1)
leave_qpi()
sel(0)

spi = SPI(0, baudrate=33_000_000, sck=Pin(18), mosi=Pin(19), miso=Pin(20))

def spi_write(data):
    try:
        cs(0)
        spi.write(data)
    finally:
        cs(1)

def reset():
    spi_write(bytes([66]))
    spi_write(bytes([99]))

def write(addr, data):
    buf = bytes((2, (addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF))
    buf += bytes(data)
    spi_write(buf)
    
def read(addr, data_len):
    data = bytearray(data_len + 4)
    data[0] = 3
    data[1] = (addr >> 16) & 0xFF
    data[2] = (addr >> 8) & 0xFF
    data[3] = addr & 0xFF
    try:
        cs(0)
        spi.write_readinto(data, data)
    finally:
        cs(1)
        
    return data[4:]