#include <stdio.h>
#include <cstring>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "display.hpp"
#include "hardware/sync.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"

extern "C" {
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"

#include "tmds_encode.h"
}

using namespace pico_stick;

namespace {
    void core1_main() {
        DisplayDriver* driver = (DisplayDriver*)multicore_fifo_pop_blocking();
        driver->run_core1();
    }
}

void DisplayDriver::run_core1() {
	dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    while (true) {
        sem_acquire_blocking(&dvi_start_sem);
        printf("Core 1 up\n");
        dvi_start(&dvi0);
        while (true) {
            const int line_counter = multicore_fifo_pop_blocking();
            uint32_t *colourbuf = (uint32_t*)multicore_fifo_pop_blocking();
            if (!colourbuf) break;

            uint32_t *tmdsbuf = (uint32_t*)multicore_fifo_pop_blocking();
            prepare_scanline(line_counter, colourbuf, tmdsbuf);
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

    for (int i = 0; i < MAX_FRAME_HEIGHT; ++i) {
        patches[i].mode = MODE_INVALID;
    }
}

void DisplayDriver::run() {
	multicore_launch_core1(core1_main);
    multicore_fifo_push_blocking(uint32_t(this));

    printf("DVI Initialized\n");
    sem_release(&dvi_start_sem);

    int16_t x = 0;
    int16_t y = 0;
    int16_t xdir = 1;
    int16_t ydir = 1;

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
        //printf("%hdx%hd\n", frame_data.config.h_length, frame_data.config.v_length);

        frame_data.get_frame_table(frame_counter, frame_table);

        update_sprites();

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
            else {
                // We are done reading RAM, indicate RAM bank can be switched
                gpio_put(PIN_VSYNC, 1);
            }
            

            // Flip the buffer index to the one read last time, which is now ready to output
            pixel_data_read_idx ^= 1;

            uint32_t *core0_tmds_buf, *core1_tmds_buf;
            queue_remove_blocking_u32(&dvi0.q_tmds_free, &core1_tmds_buf);
            multicore_fifo_push_blocking(line_counter);
            multicore_fifo_push_blocking(uint32_t(pixel_data[pixel_data_read_idx]));
            multicore_fifo_push_blocking(uint32_t(core1_tmds_buf));

            const uint32_t core1_line_length = line_lengths[pixel_data_read_idx * 2];
            uint32_t* core0_colour_buf = &pixel_data[pixel_data_read_idx][core1_line_length];

            queue_remove_blocking_u32(&dvi0.q_tmds_free, &core0_tmds_buf);
            prepare_scanline(line_counter + 1, core0_colour_buf, core0_tmds_buf);

            multicore_fifo_pop_blocking();
            queue_add_blocking_u32(&dvi0.q_tmds_valid, &core1_tmds_buf);
            queue_add_blocking_u32(&dvi0.q_tmds_valid, &core0_tmds_buf);

            line_counter += 2;
        }

        gpio_put(PIN_VSYNC, 0);

        // Temp: Move our sprite around
        x += xdir;
        y += ydir;
        if (x < 1 || x > 600) xdir = -xdir;
        if (y < 1 || y > 400) ydir = -ydir;
        set_sprite(0, true, x, y);

        // Grace period for slow RAM bank switch
        sleep_us(10);
    }
}

void DisplayDriver::set_sprite(uint8_t idx, bool enabled, int16_t x, int16_t y) {
    if (enabled) {
        sprites[idx].set_enabled(true);
        sprites[idx].set_sprite_pos(x, y);
    }
    else {
        sprites[idx].set_enabled(false);
    }
}

void __not_in_flash_func(DisplayDriver::prepare_scanline)(int line_number, uint32_t* pixel_data, uint32_t* tmds_buf) {
    auto& patch = patches[line_number];
    if (patch.mode != MODE_INVALID) {
        memcpy((uint8_t*)pixel_data + patch.offset, patch.data, patch.len);
        #if 0
        uint16_t* pixel_ptr = (uint16_t*)((uint8_t*)pixel_data + patch.offset);
        uint16_t* data_ptr = (uint16_t*)patch.data;
        for (uint32_t i = 0; i < (patch.len >> 1); ++i) {
            pixel_ptr[i] = (uint32_t(pixel_ptr[i] & 0xF7DF) + uint32_t(data_ptr[i] & 0xF7DF)) >> 1;
        }
        #endif
        patch.mode = MODE_INVALID;
    }

    tmds_encode_data_channel_fullres_16bpp(pixel_data, tmds_buf, frame_data.config.h_length, 4, 0);
    tmds_encode_data_channel_fullres_16bpp(pixel_data, tmds_buf + (frame_data.config.h_length >> 1), frame_data.config.h_length, 10, 5);
    tmds_encode_data_channel_fullres_16bpp(pixel_data, tmds_buf + frame_data.config.h_length, frame_data.config.h_length, 15, 11);
}    

void DisplayDriver::read_two_lines(uint idx) {
    uint32_t addresses[2];

    for (int i = 0; i < 2; ++i) {
        FrameTableEntry& entry = frame_table[line_counter + i];
        addresses[i] = entry.line_address();
        line_lengths[idx * 2 + i] = (frame_data.config.h_length * get_pixel_data_len(entry.line_mode())) >> 2;
    }

    ram.multi_read(addresses, &line_lengths[idx * 2], 2, pixel_data[idx]);
}

void DisplayDriver::update_sprites() {
    for (int i = 0; i < frame_data.frame_table_header.num_sprites; ++i) {
        sprites[i].update_sprite(i, frame_data, patches);
    }
}