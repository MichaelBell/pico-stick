#pragma once

#include <cstdint>

// I2C slave interface
namespace i2c_slave_if {
    // Initialize the I2C slave interface, optionally providing a callback that is made
    // after each I2C write is complete.
    // The callback argumnets are:
    //  - First register written
    //  - Last register written (same as first if only one byte written)
    //  - Pointer to first register
    void init(void (*callback)(uint8_t, uint8_t, uint8_t*) = nullptr);

    // Get the current value of a register
    uint8_t get_reg(uint8_t address);

    // Get the register memory, it is 256 bytes long and is 32-bit aligned
    uint8_t* get_reg_table();
}
