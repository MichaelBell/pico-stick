#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "display.hpp"
#include "hardware/sync.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/sem.h"

extern "C" {
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"

#include "tmds_encode.h"
}

namespace {
    struct semaphore dvi_start_sem;

    inline void prepare_scanline(const uint32_t *colourbuf, uint32_t *tmdsbuf) {
        // TODO
        constexpr int FRAME_WIDTH = 640;
        tmds_encode_data_channel_fullres_16bpp(colourbuf, tmdsbuf + 0 * FRAME_WIDTH, FRAME_WIDTH, 4, 0);
        tmds_encode_data_channel_fullres_16bpp(colourbuf, tmdsbuf + 1 * FRAME_WIDTH, FRAME_WIDTH, 10, 5);
        tmds_encode_data_channel_fullres_16bpp(colourbuf, tmdsbuf + 2 * FRAME_WIDTH, FRAME_WIDTH, 15, 11);
    }    
}

void core1_main() {
    DisplayDriver* driver = (DisplayDriver*)multicore_fifo_pop_blocking();
    driver->run_core1();
}

void DisplayDriver::run_core1() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    while (true) {
        sem_acquire_blocking(&dvi_start_sem);
        printf("Core 1 up\n");
        dvi_start(&dvi0);
        while (true) {
            const uint32_t *colourbuf = (const uint32_t*)multicore_fifo_pop_blocking();
            if (!colourbuf) break;

            uint32_t *tmdsbuf = (uint32_t*)multicore_fifo_pop_blocking();
            prepare_scanline(colourbuf, tmdsbuf);
            multicore_fifo_push_blocking(0);
        }

        // dvi_stop() - needs implementing
    }
    __builtin_unreachable();
}

void DisplayDriver::init() {
    ram.init();

    gpio_init(PIN_HEARTBEAT);
    gpio_put(PIN_HEARTBEAT, 0);
    gpio_set_dir(PIN_HEARTBEAT, GPIO_OUT);

    gpio_init(PIN_VSYNC);
    gpio_put(PIN_VSYNC, 0);
    gpio_set_dir(PIN_VSYNC, GPIO_OUT);

    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
	sem_init(&dvi_start_sem, 0, 1);
	hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
}

void DisplayDriver::run() {
	multicore_launch_core1(core1_main);
    multicore_fifo_push_blocking(uint32_t(this));

    printf("DVI Initialized\n");
    sem_release(&dvi_start_sem);

    uint heartbeat = 9;
    while (true) {
        if (++heartbeat >= 10) {
            heartbeat = 0;
            gpio_xor_mask(1u << PIN_HEARTBEAT);
        }

        if (!frame_data.read_headers()) {
            // TODO!
            return;
        }
        printf("%hdx%hd\n", frame_data.config.h_length, frame_data.config.v_length);

        frame_data.get_frame_table(frame_counter, frame_table);

        // Read first 2 lines
        line_counter = 0;
        read_two_lines(0);
        ram.wait_for_finish_blocking();

        uint pixel_data_read_idx = 1;
        while (line_counter < frame_data.config.v_length) {
            if (line_counter < frame_data.config.v_length - 2) {
                // Read two lines into the buffers we just output
                read_two_lines(pixel_data_read_idx);
            }

            // Flip the buffer index to the one read last time, which is now ready to output
            pixel_data_read_idx ^= 1;

            uint32_t *core0_tmds_buf, *core1_tmds_buf;
            queue_remove_blocking_u32(&dvi0.q_tmds_free, &core1_tmds_buf);
            multicore_fifo_push_blocking(uint32_t(pixel_data[pixel_data_read_idx]));
            multicore_fifo_push_blocking(uint32_t(core1_tmds_buf));

            const uint32_t core1_line_length = line_lengths[pixel_data_read_idx * 2];
            uint32_t* core0_colour_buf = &pixel_data[pixel_data_read_idx][core1_line_length];

            queue_remove_blocking_u32(&dvi0.q_tmds_free, &core0_tmds_buf);
            prepare_scanline(core0_colour_buf, core0_tmds_buf);

            multicore_fifo_pop_blocking();
            queue_add_blocking_u32(&dvi0.q_tmds_valid, &core1_tmds_buf);
            queue_add_blocking_u32(&dvi0.q_tmds_valid, &core0_tmds_buf);

            line_counter += 2;
        }

        // VSYNC - indicate RAM bank can be switched
        gpio_put(PIN_VSYNC, 1);
        sleep_us(10);
        gpio_put(PIN_VSYNC, 0);

        // Grace period for slow RAM bank switch
        sleep_us(10);
    }
}

void DisplayDriver::read_two_lines(uint idx) {
    uint32_t addresses[2];

    for (int i = 0; i < 2; ++i) {
        pico_stick::FrameTableEntry& entry = frame_table[line_counter + i];
        addresses[i] = entry.line_address;
        line_lengths[idx * 2 + i] = (frame_data.config.h_length * pico_stick::get_pixel_data_len((pico_stick::LineMode)entry.line_mode)) >> 2;
    }

    ram.multi_read(addresses, &line_lengths[idx * 2], 2, pixel_data[idx]);
}