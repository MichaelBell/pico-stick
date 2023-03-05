#include <cstring>
#include <algorithm>
#include "i2c_fifo.h"
#include "i2c_slave.h"
#include "pico/stdlib.h"

#include "constants.hpp"

namespace {
    constexpr uint I2C_SLAVE_ADDRESS = 0x0d;
    constexpr uint I2C_BAUDRATE = 400000;

    constexpr uint I2C_SLAVE_SDA_PIN = 10;
    constexpr uint I2C_SLAVE_SCL_PIN = 11;
    constexpr i2c_inst_t* I2C_INSTANCE = i2c1;

    constexpr uint I2C_SPRITE_REG_BASE = 0;
    constexpr uint I2C_SPRITE_DATA_LEN = 7;

    constexpr uint I2C_HIGH_REG_BASE = 0xC0;
    constexpr uint I2C_NUM_HIGH_REGS = 0x40;

    // Callback made after an I2C write to high registers is complete.  It gives the first register written,
    // The last register written, and a pointer to the memory representing all high registers (from 0xC0).
    void (*i2c_reg_written_callback)(uint8_t, uint8_t, uint8_t*) = nullptr;

    // Calback made after an I2C write to sprite memory.  It gives the index of the first sprite written,
    // number of bytes written (this may go on to further sprites), and a pointer to the memory
    // holding all of the sprite info.
    void (*i2c_sprite_written_callback)(uint8_t, uint8_t, uint8_t*) = nullptr;

    // To write a series of bytes, the master first
    // writes the memory address, followed by the data. The address is automatically incremented
    // for each byte transferred, looping back to 0 upon reaching the end. Reading is done
    // sequentially from the current memory address.
    struct I2CContext
    {
        uint8_t sprite_mem[MAX_SPRITES * I2C_SPRITE_DATA_LEN];
        alignas(4) uint8_t high_regs[I2C_NUM_HIGH_REGS];
        uint16_t cur_register;
        uint8_t first_register;
        uint8_t access_idx;
        bool got_register;
        bool data_written;
    } context;

    // Our handler is called from the I2C ISR, so it must complete quickly. Blocking calls /
    // printing to stdio may interfere with interrupt handling.
    void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    struct I2CContext* cxt = &context;
        switch (event) {
        case I2C_SLAVE_RECEIVE: // master has written some data
            if (!cxt->got_register) {
                // writes always start with the memory address
                cxt->cur_register = i2c_read_byte(i2c);
                cxt->first_register = cxt->cur_register;
                cxt->access_idx = 0;
                cxt->got_register = true;
                cxt->data_written = false;
            } else if (cxt->cur_register >= I2C_SPRITE_REG_BASE && cxt->cur_register < I2C_SPRITE_REG_BASE + MAX_SPRITES) {
                // save into memory
                cxt->sprite_mem[cxt->cur_register * I2C_SPRITE_DATA_LEN + cxt->access_idx] = i2c_read_byte(i2c);
                if (++cxt->access_idx == I2C_SPRITE_DATA_LEN) {
                    cxt->access_idx = 0;
                    ++cxt->cur_register;
                }
                cxt->data_written = true;
            } else if (cxt->cur_register >= I2C_HIGH_REG_BASE && cxt->cur_register < I2C_HIGH_REG_BASE + I2C_NUM_HIGH_REGS) {
                cxt->high_regs[cxt->cur_register - I2C_HIGH_REG_BASE] = i2c_read_byte(i2c);
                ++cxt->cur_register;
                cxt->data_written = true;
            } else {
                ++cxt->cur_register;
            }
            break;
        case I2C_SLAVE_REQUEST: // master is requesting data
            // load from memory
            if (cxt->cur_register >= I2C_SPRITE_REG_BASE && cxt->cur_register < I2C_SPRITE_REG_BASE + MAX_SPRITES) {
                i2c_write_byte(i2c, cxt->sprite_mem[cxt->cur_register + cxt->access_idx]);
                if (++cxt->access_idx == I2C_SPRITE_DATA_LEN) {
                    cxt->access_idx = 0;
                    ++cxt->cur_register;
                }
            } else if (cxt->cur_register >= I2C_HIGH_REG_BASE && cxt->cur_register < I2C_HIGH_REG_BASE + I2C_NUM_HIGH_REGS) {
                i2c_write_byte(i2c, cxt->high_regs[cxt->cur_register - I2C_HIGH_REG_BASE]);
                ++cxt->cur_register;
            } else {
                ++cxt->cur_register;
            }
            break;
        case I2C_SLAVE_FINISH: // master has signalled Stop / Restart
            if (cxt->data_written) {
                //printf("I2C: W%02hhx-%02hhx\n", cxt->first_register, cxt->cur_register-1);
                if (cxt->first_register >= I2C_SPRITE_REG_BASE && cxt->first_register < I2C_SPRITE_REG_BASE + MAX_SPRITES) {
                    if (i2c_sprite_written_callback) {
                        if (cxt->access_idx == 0) cxt->cur_register--;
                        i2c_sprite_written_callback(cxt->first_register, std::min(cxt->cur_register, uint16_t(I2C_SPRITE_REG_BASE + MAX_SPRITES - 1)), cxt->sprite_mem);
                    }
                } else if (cxt->first_register >= I2C_HIGH_REG_BASE && cxt->first_register < I2C_HIGH_REG_BASE + I2C_NUM_HIGH_REGS) {
                    if (i2c_reg_written_callback) {
                        i2c_reg_written_callback(cxt->first_register, std::min(cxt->cur_register-1, int(I2C_HIGH_REG_BASE + I2C_NUM_HIGH_REGS - 1)), cxt->high_regs);
                    }
                }
            }
            else if (cxt->cur_register > cxt->first_register) {
                //printf("I2C: R%02hhx-%02hhx\n", cxt->first_register, cxt->cur_register-1);
            }
            cxt->got_register = false;
            break;
        default:
            break;
        }
    }
}

namespace i2c_slave_if {
    uint8_t* init(void (*sprite_callback)(uint8_t, uint8_t, uint8_t*), void (*reg_callback)(uint8_t, uint8_t, uint8_t*)) {
        i2c_reg_written_callback = reg_callback;
        i2c_sprite_written_callback = sprite_callback;

        memset(context.sprite_mem, 0xFF, MAX_SPRITES * I2C_SPRITE_DATA_LEN);
        memset(context.high_regs, 0, I2C_NUM_HIGH_REGS);

        gpio_init(I2C_SLAVE_SDA_PIN);
        gpio_set_function(I2C_SLAVE_SDA_PIN, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_SLAVE_SDA_PIN);

        gpio_init(I2C_SLAVE_SCL_PIN);
        gpio_set_function(I2C_SLAVE_SCL_PIN, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_SLAVE_SCL_PIN);

        i2c_init(I2C_INSTANCE, I2C_BAUDRATE);
        i2c_slave_init(I2C_INSTANCE, I2C_SLAVE_ADDRESS, &i2c_slave_handler);

        return context.high_regs;
    }

    void deinit() {
        i2c_slave_deinit(I2C_INSTANCE);
        i2c_deinit(I2C_INSTANCE);
    }

    uint8_t get_reg(uint8_t reg) {
        return context.high_regs[reg - I2C_HIGH_REG_BASE];
    }

    uint8_t* get_high_reg_table() {
        return context.high_regs;
    }
}
