#include <stdio.h>
#include <cstring>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "display.hpp"
#include "hardware/sync.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/pwm.h"
#include "pico/multicore.h"

#include "pins.hpp"

extern "C" {
#include "dvi_serialiser.h"
#include "common_dvi_pin_configs.h"

#include "tmds_double_encode.h"
#include "tmds_encode.h"
}

using namespace pico_stick;

#define PROFILE_SCANLINE 0
#define PROFILE_SCANLINE_MAX 0
#define PROFILE_VSYNC 0

#define TEST_SPRITES 0

static pico_stick::FrameTableEntry __attribute__((section(".usb_ram.frame_table"))) the_frame_table[MAX_FRAME_HEIGHT];

DisplayDriver::DisplayDriver(PIO pio)
    : frame_data(ram)
    , current_res(RESOLUTION_720x480)
    , ram(PIN_RAM_CS, PIN_RAM_D0)
    , dvi0{
        .timing{&dvi_timing_720x480p_60hz},
        .ser_cfg{
            .pio = pio,
            .sm_tmds = {0, 1, 2},
            .pins_tmds = {PIN_HDMI_D0, PIN_HDMI_D1, PIN_HDMI_D2},
            .pins_clk = PIN_HDMI_CLK,
            .invert_diffpairs = true}}
{
    frame_table = the_frame_table;
}

bool DisplayDriver::set_res(pico_stick::Resolution res)
{
    const dvi_timing* normal_modes[] = {
        &dvi_timing_640x480p_60hz,
        &dvi_timing_720x480p_60hz,
        &dvi_timing_720x400p_70hz,
        &dvi_timing_720x576p_50hz,
    };

    if (res <= RESOLUTION_720x576) {
        dvi0.timing = normal_modes[(int)res];
        current_res = res;
        printf("Set res %d\n", res);
        return true;
    }

#if SUPPORT_WIDE_MODES
    const dvi_timing* wide_modes[] = {
        &dvi_timing_800x600p_60hz,
        &dvi_timing_800x480p_60hz,
        &dvi_timing_800x450p_60hz,
        &dvi_timing_960x540p_60hz,
        &dvi_timing_960x540p_50hz,
        &dvi_timing_1280x720p_30hz,
    };

    if (res >= RESOLUTION_800x600 && res <= RESOLUTION_1280x720) {
        dvi0.timing = wide_modes[(int)res - (int)RESOLUTION_800x600];
        current_res = res;
        return true;
    }
#endif

    return false;
}

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
            uint32_t line_counter = multicore_fifo_pop_blocking();
            const int8_t lmode = (line_counter & 0xFF000000u) >> 24;
            line_counter &= 0xFFFFFFu;
            uint32_t *colourbuf = (uint32_t*)multicore_fifo_pop_blocking();
            if (!colourbuf) break;

            uint32_t *tmdsbuf = (uint32_t*)multicore_fifo_pop_blocking();
            prepare_scanline_core1(line_counter, colourbuf, tmdsbuf, lmode);
            multicore_fifo_push_blocking(0);
        }
    }
    __builtin_unreachable();
}

void DisplayDriver::init() {
    //ram.init();

    if (!ever_inited) {
        gpio_init(PIN_VSYNC);
        gpio_put(PIN_VSYNC, 0);
        gpio_set_dir(PIN_VSYNC, GPIO_OUT);

        // Setup TMDS symbol LUTs
        tmds_double_encode_setup_default_lut(tmds_15bpp_lut);
        memcpy(tmds_palette_luts + (PALETTE_SIZE * PALETTE_SIZE * 6), tmds_15bpp_lut, PALETTE_SIZE * PALETTE_SIZE * 2);
        memcpy(tmds_palette_luts + (PALETTE_SIZE * PALETTE_SIZE * 10), tmds_15bpp_lut, PALETTE_SIZE * PALETTE_SIZE * 2);

        dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
        for (int i = 0; i < NUM_TMDS_BUFFERS; ++i) {
            void* bufptr = (void*)&tmds_buffers[i * 3 * MAX_FRAME_WIDTH / DVI_SYMBOLS_PER_WORD];
            queue_add_blocking_u32(&dvi0.q_tmds_free, &bufptr);
        }
        sem_init(&dvi_start_sem, 0, 1);
        hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);

        Sprite::init();

        for (int i = 0; i < MAX_FRAME_HEIGHT; ++i) {
            for (int j = 0; j < MAX_PATCHES_PER_LINE; ++j) {
                patches[i][j].data = nullptr;
            }
        }

        ever_inited = true;
    }
    else {
        dvi_setup(&dvi0);
    }

    // This calculation shouldn't overflow for any resolution we could plausibly support.
    const uint32_t pixel_clk_khz = dvi0.timing->bit_clk_khz / 10;
    const uint32_t scanline_pixels = dvi0.timing->h_front_porch + dvi0.timing->h_sync_width + dvi0.timing->h_back_porch + dvi0.timing->h_active_pixels;
    diags.available_vsync_time = (1000u * (dvi0.timing->v_front_porch + dvi0.timing->v_sync_width + dvi0.timing->v_back_porch) * 
                                  scanline_pixels) / pixel_clk_khz;
    diags.available_total_scanline_time = (1000u * dvi0.timing->v_active_lines * scanline_pixels) / pixel_clk_khz;
    diags.available_time_per_scanline = (1000u * scanline_pixels) / pixel_clk_khz;
    printf("%dx%d active area\n", dvi0.timing->h_active_pixels, dvi0.timing->v_active_lines);
    printf("Available VSYNC time: %luus\n", diags.available_vsync_time);
    printf("Available time for all active scanlines: %luus\n", diags.available_total_scanline_time);
    printf("Available time per scanline: %luus\n", diags.available_time_per_scanline);
}

void DisplayDriver::run() {
    if (!frame_data.read_headers()) {
        printf("Failed to read header\n");
        return;
    }
    printf("Configured display size %dx%d, v rep=%d\n", frame_data.config.h_length, frame_data.config.v_length, frame_data.config.v_repeat);
    dvi0.vertical_repeat = frame_data.config.v_repeat;

	multicore_launch_core1(core1_main);
    multicore_fifo_push_blocking(uint32_t(this));

    printf("DVI Initialized\n");
    sem_release(&dvi_start_sem);

#if TEST_SPRITES
    constexpr int num_sprites = MAX_SPRITES;
    int32_t x[num_sprites];
    int32_t y[num_sprites];
    int32_t xdir[num_sprites];
    int32_t ydir[num_sprites];
    constexpr int sprite_move_shift = 7;

    for (int i = 0; i < num_sprites; ++i) {
        #if 1
        x[i] = (rand() % 720) << sprite_move_shift;
        y[i] = (rand() % 480) << sprite_move_shift;
        xdir[i] = (rand() % 61) - 30;
        ydir[i] = (rand() % 61) - 30;
        #else
        x[i] = (i*18) << sprite_move_shift; //(rand() % 640) << sprite_move_shift;
        y[i] = (36 * (1 + (i >> 3))) << sprite_move_shift; //(120 + i) << sprite_move_shift;
        xdir[i] = 0;
        ydir[i] = 0;
        #endif
    }
#endif

    bool first_frame = true;
    uint heartbeat = 9;
    //int frame_address_dir = 4;
    while (!stop_display) {
        if (heartbet_led) {
            uint val;
            if (heartbeat < 32) val = heartbeat << 3;
            else if (heartbeat == 32) val = 255;
            else val = (64 - heartbeat) << 3;
            if (++heartbeat == 64) heartbeat = 0;
            pwm_set_gpio_level(PIN_LED, val * val);
        }

        uint32_t vsync_start_time = time_us_32();

        if (spi_mode) {
            ram.set_qpi();
        }

        if (!frame_data.read_headers()) {
            // TODO!
            return;
        }
        //printf("%hdx%hd\n", frame_data.config.h_length, frame_data.config.v_length);

        // Update frame counter
        if (frame_data.frame_table_header.bank_number != last_bank) {
            frame_counter = frame_data.frame_table_header.first_frame;
            last_bank = frame_data.frame_table_header.bank_number;
            frames_to_next_count = frame_data.frame_table_header.frame_rate_divider;

            if (frame_data.frame_table_header.palette_advance || palette_idx >= frame_data.frame_table_header.num_palettes) {
                palette_idx = 0;
            }
        }
        else if (frame_data.frame_table_header.frame_rate_divider != 0)
        {
            if (--frames_to_next_count <= 0) {
                if (++frame_counter >= frame_data.frame_table_header.num_frames) {
                    frame_counter = 0;
                }
                if (frame_data.frame_table_header.palette_advance && ++palette_idx >= frame_data.frame_table_header.num_palettes) {
                    palette_idx = 0;
                }
                frames_to_next_count = frame_data.frame_table_header.frame_rate_divider;
            }
        }

        //pwm_set_gpio_level(PIN_LED, frame_data.frame_table_header.bank_number ? 255*255 : 0);

        frame_data.get_frame_table(frame_counter, frame_table);

        if (frame_data.config.v_repeat != dvi0.vertical_repeat) {
            printf("Changing v repeat to %d\n", frame_data.config.v_repeat);
            // Wait until it is safe to change the vertical repeat
            while (dvi0.timing_state.v_state == DVI_STATE_ACTIVE)
                __compiler_memory_barrier();
            dvi0.vertical_repeat = frame_data.config.v_repeat;
        }

        setup_palette();

        update_sprites();

        // Update offsets
        for (int i = 1; i < NUM_SCROLL_GROUPS; ++i) {
            frame_scroll[i] = next_frame_scroll[i];
        }

        // Read first 2 lines
        line_counter = 0;
        read_two_lines(0);
        ram.wait_for_finish_blocking();
        line_counter = 2;

        diags.peak_scanline_time = std::max(diags.peak_scanline_time, std::max(diags.scanline_max_prep_time[0], diags.scanline_max_prep_time[1]));
        diags.vsync_time = time_us_32() - vsync_start_time;
#if PROFILE_SCANLINE
#if PROFILE_SCANLINE_MAX
        printf("Ln %luus, lt: %d\n", diags.scanline_max_prep_time[0] + diags.scanline_max_prep_time[1], dvi0.total_late_scanlines);
#else
        printf("Ln %luus, lt: %d\n", diags.scanline_total_prep_time[0] + diags.scanline_total_prep_time[1], dvi0.total_late_scanlines);
#endif
#endif
#if PROFILE_VSYNC
        printf("VSYNC %luus, late: %d\n", diags.vsync_time, dvi0.total_late_scanlines);
#endif

        if (diags_callback) {
            diags.total_late_scanlines = dvi0.total_late_scanlines;
            diags_callback(diags);
        }

        // Clear per frame diags
        diags.scanline_total_prep_time[0] = 0;
        diags.scanline_total_prep_time[1] = 0;
        diags.scanline_max_prep_time[0] = 0;
        diags.scanline_max_prep_time[1] = 0;
        diags.scanline_max_sprites[0] = 0;
        diags.scanline_max_sprites[1] = 0;

        main_loop();

        gpio_put(PIN_VSYNC, 0);

        // Grace period for slow RAM bank switch
        sleep_us(10);

#if TEST_SPRITES
        // Temp: Move our sprites around
        for (int i = 0; i < num_sprites; ++i) {
            x[i] += xdir[i];
            y[i] += ydir[i];
            if (x[i] < (-20 << sprite_move_shift) && xdir[i] < 0) xdir[i] = -xdir[i];
            if (x[i] > (720 << sprite_move_shift) && xdir[i] > 0) xdir[i] = -xdir[i];
            if (y[i] < (-20 << sprite_move_shift) && ydir[i] < 0) ydir[i] = -ydir[i];
            if (y[i] > (480 << sprite_move_shift) && ydir[i] > 0) ydir[i] = -ydir[i];
            #if 1
            BlendMode blend_mode = BLEND_NONE;
            #if 1
            if (i & 1) {
                if (i & 2) {
                    blend_mode = BLEND_BLEND;
                }
                else {
                    blend_mode = BLEND_DEPTH;
                }
            } else {
                if (i & 2) {
                    blend_mode = BLEND_BLEND2;
                }
                else {
                    blend_mode = BLEND_DEPTH2;
                }
            }
            #else
            blend_mode = BLEND_BLEND;
            #endif
            if (i & 1)
                set_sprite(i, 4, blend_mode, x[i] >> sprite_move_shift, y[i] >> sprite_move_shift);
            else
                set_sprite(i, ((i + heartbeat) >> 3) & 3, blend_mode, x[i] >> sprite_move_shift, y[i] >> sprite_move_shift);
                #endif
        }
#endif

#if 0
        if (heartbeat == 0) {
            frame_data_address_offset += frame_address_dir;
            if (frame_data_address_offset >= 255 * 4) {
                frame_address_dir = -4;
            }
            else if (frame_data_address_offset <= 0) {
                frame_address_dir = 4;
            }
        }
#endif

        if (first_frame) {
            dvi0.total_late_scanlines = 0;
            first_frame = false;
        }
    }

    dvi_stop(&dvi0);
    sleep_ms(1);

    memset(frame_scroll, 0, sizeof(frame_scroll));
    memset(next_frame_scroll, 0, sizeof(next_frame_scroll));

    for (int i = 0; i < MAX_SPRITES; ++i) {
        clear_sprite(i);
    }

    if (heartbet_led) {
        pwm_set_gpio_level(PIN_LED, 0);
    }

    stop_display = false;
}

void DisplayDriver::main_loop() {
    uint pixel_data_read_idx = 1;
    while (line_counter < frame_data.config.v_length + 2) {
        if (line_counter < frame_data.config.v_length) {
            // Read two lines into the buffers we just output
            read_two_lines(pixel_data_read_idx);
        }
        else {
            // We are done reading RAM, indicate RAM bank can be switched
            if (spi_mode) {
                ram.set_spi();
            }

            gpio_put(PIN_VSYNC, 1);
        }

        // Flip the buffer index to the one read last time, which is now ready to output
        pixel_data_read_idx ^= 1;

        uint32_t *core0_tmds_buf = nullptr, *core1_tmds_buf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &core1_tmds_buf);
        sio_hw->fifo_wr = (line_counter - 2) | (line_mode[pixel_data_read_idx * 2] << 24);
        sio_hw->fifo_wr = uint32_t(pixel_ptr[pixel_data_read_idx * 2]);
        sio_hw->fifo_wr = uint32_t(core1_tmds_buf);
        __sev();

        if (line_counter < frame_data.config.v_length + 1) {
            uint32_t* core0_colour_buf = pixel_ptr[pixel_data_read_idx * 2 + 1];

            queue_remove_blocking_u32(&dvi0.q_tmds_free, &core0_tmds_buf);
            prepare_scanline_core0(line_counter - 1, core0_colour_buf, core0_tmds_buf, line_mode[pixel_data_read_idx * 2 + 1]);
        }

        multicore_fifo_pop_blocking();
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &core1_tmds_buf);
        if (line_counter < frame_data.config.v_length + 1) {
            queue_add_blocking_u32(&dvi0.q_tmds_valid, &core0_tmds_buf);
        }

        line_counter += 2;
    }
}

void DisplayDriver::set_sprite(int8_t i, int16_t idx, BlendMode mode, int16_t x, int16_t y, uint8_t v_scale) {
    if (i < MAX_SPRITES) {
        sprites[i].set_sprite_table_idx(idx);
        sprites[i].set_blend_mode(mode);
        sprites[i].set_sprite_pos(x, y);
        sprites[i].set_sprite_v_scale(v_scale);
    }
}

void DisplayDriver::move_sprite(int8_t i, int16_t x, int16_t y) {
    sprites[i].set_sprite_pos(x, y);
}

void DisplayDriver::clear_sprite(int8_t i) {
    sprites[i].set_sprite_table_idx(-1);
}

void DisplayDriver::clear_late_scanlines() {
    dvi0.total_late_scanlines = 0;
}

void DisplayDriver::prepare_scanline_core0(int line_number, uint32_t* pixel_data, uint32_t* tmds_buf, int scanline_mode) {
    uint32_t start = time_us_32();

    int i;
    for (i = 0; i < MAX_PATCHES_PER_LINE; ++i) {
        if (patches[line_number][i].data) {
            if (scanline_mode & (RGB888 | PALETTE)) Sprite::apply_blend_patch_byte_x(patches[line_number][i], (uint8_t*)pixel_data);
            else Sprite::apply_blend_patch_555_y(patches[line_number][i], (uint8_t*)pixel_data);
            patches[line_number][i].data = nullptr;
        }
        else {
            break;
        }
    }
    if (scanline_mode & DOUBLE_PIXELS) {
        if ((scanline_mode & (PALETTE | RGB888)) == (PALETTE | RGB888)) tmds_encode_palette_data(pixel_data, tmds_doubled_palette256_lut, tmds_buf, frame_data.config.h_length >> 1, 0, 8);
        else if (scanline_mode & RGB888) tmds_encode_24bpp(pixel_data, tmds_buf, frame_data.config.h_length >> 1);
        else if (scanline_mode & PALETTE) tmds_encode_palette_data(pixel_data, tmds_doubled_palette_lut, tmds_buf, frame_data.config.h_length >> 1, 2, 5);
        else tmds_encode_15bpp(pixel_data, tmds_buf, frame_data.config.h_length >> 1);
    }
    else if (scanline_mode & PALETTE) tmds_encode_fullres_palette(pixel_data, tmds_palette_luts, tmds_buf, frame_data.config.h_length);
    else tmds_encode_fullres_15bpp(pixel_data, tmds_15bpp_lut, tmds_buf, frame_data.config.h_length);

    const uint32_t scanline_time = time_us_32() - start;
    diags.scanline_max_prep_time[0] = std::max(scanline_time, diags.scanline_max_prep_time[0]);
    diags.scanline_max_sprites[0] = std::max(uint32_t(i), diags.scanline_max_sprites[0]);
    diags.scanline_total_prep_time[0] += scanline_time;
}    

void DisplayDriver::prepare_scanline_core1(int line_number, uint32_t* pixel_data, uint32_t* tmds_buf, int scanline_mode) {
    uint32_t start = time_us_32();

    int i;
    for (i = 0; i < MAX_PATCHES_PER_LINE; ++i) {
        if (patches[line_number][i].data) {
            if (scanline_mode & (RGB888 | PALETTE)) Sprite::apply_blend_patch_byte_x(patches[line_number][i], (uint8_t*)pixel_data);
            else Sprite::apply_blend_patch_555_x(patches[line_number][i], (uint8_t*)pixel_data);
            patches[line_number][i].data = nullptr;
        }
        else {
            break;
        }
    }
    if (scanline_mode & DOUBLE_PIXELS) {
        if ((scanline_mode & (PALETTE | RGB888)) == (PALETTE | RGB888)) tmds_encode_palette_data(pixel_data, tmds_doubled_palette256_lut, tmds_buf, frame_data.config.h_length >> 1, 0, 8);
        else if (scanline_mode & RGB888) tmds_encode_24bpp(pixel_data, tmds_buf, frame_data.config.h_length >> 1);
        else if (scanline_mode & PALETTE) tmds_encode_palette_data(pixel_data, tmds_doubled_palette_lut, tmds_buf, frame_data.config.h_length >> 1, 2, 5);
        else tmds_encode_15bpp(pixel_data, tmds_buf, frame_data.config.h_length >> 1);
    }
    else if (scanline_mode & PALETTE) tmds_encode_fullres_palette(pixel_data, tmds_palette_luts, tmds_buf, frame_data.config.h_length);
    else tmds_encode_fullres_15bpp(pixel_data, tmds_15bpp_lut, tmds_buf, frame_data.config.h_length);

    const uint32_t scanline_time = time_us_32() - start;
    diags.scanline_max_prep_time[1] = std::max(scanline_time, diags.scanline_max_prep_time[1]);
    diags.scanline_max_sprites[1] = std::max(uint32_t(i), diags.scanline_max_sprites[1]);
    diags.scanline_total_prep_time[1] += scanline_time;
}    

void DisplayDriver::read_two_lines(uint idx) {
    uint32_t addresses[4];
    uint32_t read_lengths[4];
    uint32_t* ptr = pixel_data[idx];
    int address_idx = 0;

    for (int i = 0; i < 2; ++i) {
        const FrameTableEntry& entry = frame_table[line_counter + i];
        const ScrollConfig& scroll_config = frame_scroll[entry.frame_offset_idx()];
        
        uint32_t addr = entry.line_address() + scroll_config.start_address_offset;
        if (scroll_config.max_start_address > 0 && addr >= scroll_config.max_start_address) {
            addr = entry.line_address() + scroll_config.start_address_offset2;
        }

        addresses[address_idx] = addr;
        pixel_ptr[idx * 2 + i] = ptr;

        const bool double_pixels = (entry.h_repeat() == 2);
        uint32_t line_length = frame_data.config.h_length * get_pixel_data_len(entry.line_mode());
        if (double_pixels) line_length >>= 1;
        ptr += line_length >> 2;

        if (scroll_config.wrap_position > 0 && (uint32_t)scroll_config.wrap_position < line_length) {
            read_lengths[address_idx++] = scroll_config.wrap_position;
            addresses[address_idx] = addr + scroll_config.wrap_offset;
            read_lengths[address_idx++] = line_length - scroll_config.wrap_position;
        }
        else {
            read_lengths[address_idx++] = line_length;
        }
        
        int8_t lmode = 0;
        if (double_pixels) lmode |= DOUBLE_PIXELS;
        if (entry.line_mode() == MODE_PALETTE256) lmode |= PALETTE | RGB888;
        else if (entry.line_mode() == MODE_PALETTE) lmode |= PALETTE;
        else if (entry.line_mode() == MODE_RGB888) lmode |= RGB888;
        line_mode[idx * 2 + i] = lmode;
    }

    ram.multi_read(addresses, read_lengths, address_idx, pixel_data[idx]);
}

void DisplayDriver::setup_palette() {
    if (frame_data.frame_table_header.num_palettes == 0) return;

    uint8_t palette[PALETTE_SIZE * 3];
    frame_data.get_palette(palette_idx, frame_counter, palette);
    ram.wait_for_finish_blocking();

    tmds_double_encode_setup_lut(palette, tmds_palette_luts, 3);
    tmds_double_encode_setup_lut(palette + 1, tmds_palette_luts + (PALETTE_SIZE * PALETTE_SIZE * 4), 3);
    tmds_double_encode_setup_lut(palette + 2, tmds_palette_luts + (PALETTE_SIZE * PALETTE_SIZE * 8), 3);
    tmds_setup_palette_symbols(palette, tmds_doubled_palette_lut, PALETTE_SIZE, 32);

    if (frame_data.frame_table_header.num_palettes >= palette_idx + 8) {
        // 256 colour palette (pixel doubled only)
        tmds_setup_palette_symbols(palette, tmds_doubled_palette256_lut, 32, 256);
        for (int i = 1; i < 8; ++i) {
            frame_data.get_palette(palette_idx + i, frame_counter, palette);
            ram.wait_for_finish_blocking();
            tmds_setup_palette_symbols(palette, tmds_doubled_palette256_lut + 32 * i, 32, 256);
        }
    }
}

void DisplayDriver::update_sprites() {
    Sprite::clear_sprite_data();
    for (int i = 0; i < MAX_SPRITES; ++i) {
        int16_t sprite_table_idx = sprites[i].get_sprite_table_idx();
        if (sprite_table_idx < 0) continue;

        bool copied = false;
        for (int j = 0; j < i; ++j) {
            if (sprites[j].get_sprite_table_idx() == sprite_table_idx) {
                sprites[i].copy_sprite(sprites[j]);
                copied = true;
                break;
            }
        }
        
        if (!copied) sprites[i].update_sprite(frame_data);
        sprites[i].setup_patches(*this);
    }
}