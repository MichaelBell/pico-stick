#pragma once

#include <cstdint>

// I2C slave interface
namespace i2c_slave_if {
    // Initialize the I2C slave interface, optionally providing callbacks that are made
    // after each I2C write is complete.
    // The sprite callback arguments are:
    //  - First sprite written
    //  - Last sprite written (same as first if only one sprite written)
    //  - Pointer start of sprite memory (for all sprites)
    // Similarly the register callback arguments are:
    //  - First register written
    //  - Last register written (same as first if only one byte written)
    //  - Pointer start of high register memory (for all registers, the pointer points at register 0xC0)
    // The init call returns the pointer to high register memory, so that it can be properly initialized.
    uint8_t* init(void (*sprite_callback)(uint8_t, uint8_t, uint8_t*), void (*reg_callback)(uint8_t, uint8_t, uint8_t*));

    // Deinitialize before adjusting clocks, then init again.
    void deinit();

    // Get the current value of a high register
    uint8_t get_reg(uint8_t reg);

    // Get the high register memory, it is 64 bytes long and is 32-bit aligned
    uint8_t* get_high_reg_table();
}
