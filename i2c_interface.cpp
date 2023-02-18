#include <cstring>
#include "i2c_fifo.h"
#include "i2c_slave.h"
#include "pico/stdlib.h"

namespace {
    constexpr uint I2C_SLAVE_ADDRESS = 0x0d;
    constexpr uint I2C_BAUDRATE = 400000;

    constexpr uint I2C_SLAVE_SDA_PIN = 10;
    constexpr uint I2C_SLAVE_SCL_PIN = 11;

    // Callback made after an I2C write is complete.  It gives the first register written,
    // The last register written, and a pointer to the memory representing the first register.
    void (*i2c_written_callback)(uint8_t, uint8_t, uint8_t*) = nullptr;

    // The slave implements a 256 byte memory. To write a series of bytes, the master first
    // writes the memory address, followed by the data. The address is automatically incremented
    // for each byte transferred, looping back to 0 upon reaching the end. Reading is done
    // sequentially from the current memory address.
    struct I2CMemContext
    {
        alignas(4) uint8_t mem[256];
        uint8_t mem_address;
        uint8_t mem_first_address;
        bool mem_address_written;
    } context;

    // Our handler is called from the I2C ISR, so it must complete quickly. Blocking calls /
    // printing to stdio may interfere with interrupt handling.
    void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event) {
    struct I2CMemContext* cxt = &context;
        switch (event) {
        case I2C_SLAVE_RECEIVE: // master has written some data
            if (!cxt->mem_address_written) {
                // writes always start with the memory address
                cxt->mem_address = i2c_read_byte(i2c);
                cxt->mem_first_address = cxt->mem_address;
                cxt->mem_address_written = true;
            } else {
                // save into memory
                cxt->mem[cxt->mem_address] = i2c_read_byte(i2c);
                cxt->mem_address++;
            }
            break;
        case I2C_SLAVE_REQUEST: // master is requesting data
            // load from memory
            i2c_write_byte(i2c, cxt->mem[cxt->mem_address]);
            cxt->mem_address++;
            break;
        case I2C_SLAVE_FINISH: // master has signalled Stop / Restart
            cxt->mem_address_written = false;
            if (cxt->mem_first_address != cxt->mem_address && i2c_written_callback) {
                i2c_written_callback(cxt->mem_first_address, cxt->mem_address-1, &cxt->mem[cxt->mem_first_address]);
            }
            break;
        default:
            break;
        }
    }
}

namespace i2c_slave_if {
    void init(void (*callback)(uint8_t, uint8_t, uint8_t*)) {
        i2c_written_callback = callback;

        memset(context.mem, 0, 256);

        gpio_init(I2C_SLAVE_SDA_PIN);
        gpio_set_function(I2C_SLAVE_SDA_PIN, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_SLAVE_SDA_PIN);

        gpio_init(I2C_SLAVE_SCL_PIN);
        gpio_set_function(I2C_SLAVE_SCL_PIN, GPIO_FUNC_I2C);
        gpio_pull_up(I2C_SLAVE_SCL_PIN);

        i2c_init(i2c1, I2C_BAUDRATE);
        i2c_slave_init(i2c1, I2C_SLAVE_ADDRESS, &i2c_slave_handler);
    }

    uint8_t get_reg(uint8_t address) {
        return context.mem[address];
    }

    uint8_t* get_reg_table() {
        return context.mem;
    }
}
