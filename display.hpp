#pragma once

#include <map>

#include "pico/sem.h"
#include "aps6404.hpp"
extern "C"
{
#include "dvi.h"
#include "dvi_timing.h"
}
#include "constants.hpp"
#include "frame_decode.hpp"
#include "sprite.hpp"

class DisplayDriver
{
public:
    DisplayDriver(PIO pio = pio1);

    // May only be called before init
    bool set_res(pico_stick::Resolution res);
    pico_stick::Resolution get_res() const { return current_res; }

    void init();

    // Runs the display.  This never returns and also starts processing on core1.
    // The resolution and hence timing are specified in the PSRAM
    // so this sets up/resets the DVI appropriately as they change.
    void run();

    // Setup a sprite with data and position
    void set_sprite(int8_t i, int16_t table_idx, pico_stick::BlendMode mode, int16_t x, int16_t y);

    // Move an existing sprite
    void move_sprite(int8_t i, int16_t x, int16_t y);

    // Disbale a sprite
    void clear_sprite(int8_t i);

    void set_frame_data_address_offset(int offset)
    {
        frame_data_address_offset = offset;
    }

    // Called internally by run().
    void run_core1();

    pimoroni::APS6404 &get_ram() { return ram; }
    uint32_t get_clock_khz() { return dvi0.timing->bit_clk_khz; }

    // Diagnostic data - all times in us.
    struct Diags {
        uint32_t scanline_total_prep_time[2] = {0, 0};
        uint32_t scanline_max_prep_time[2] = {0, 0};
        uint32_t scanline_max_sprites[2] = {0, 0};
        uint32_t vsync_time = 0;
        uint32_t peak_scanline_time = 0;
        uint32_t total_late_scanlines = 0;
        uint32_t available_total_scanline_time = 0;
        uint32_t available_time_per_scanline = 0;
        uint32_t available_vsync_time = 0;
    };
    const Diags& get_diags() const { return diags; }
    void clear_peak_scanline_time() { diags.peak_scanline_time = 0; }
    void clear_late_scanlines();

    // Set this callback to get diags info each frame before it is cleared
    void (*diags_callback)(const Diags&) = nullptr;

    // Defaults to QPI.  If use_spi true then RAM set back to SPI mode for VSYNC.
    void set_spi_mode(bool use_spi) { spi_mode = use_spi; }

    void enable_heartbeat(bool enable) { heartbet_led = enable; }

private:
    friend class Sprite;

    void main_loop();
    void prepare_scanline_core0(int line_number, uint32_t *pixel_data, uint32_t *tmds_buf, bool double_pixels);
    void prepare_scanline_core1(int line_number, uint32_t *pixel_data, uint32_t *tmds_buf, bool double_pixels);
    void read_two_lines(uint idx);
    void clear_patches();
    void update_sprites();
    uint32_t get_line_address(int line_number)
    {
        auto &entry = frame_table[line_number];
        return entry.line_address() + (entry.apply_frame_offset() ? frame_data_address_offset : 0);
    }

    FrameDecode frame_data;
    pico_stick::Resolution current_res;

    pimoroni::APS6404 ram;
    struct dvi_inst dvi0;
    struct semaphore dvi_start_sem;

    int frame_counter = 0;
    int line_counter = 0;

    int frame_data_address_offset = 0;

    // Must be as long as the greatest supported frame height.
    pico_stick::FrameTableEntry* frame_table;

    // Patches that require blending, done by CPU
    Sprite::BlendPatch patches[MAX_FRAME_HEIGHT][MAX_PATCHES_PER_LINE];

    // Must be long enough to accept two lines at maximum data length and maximum width
    uint32_t pixel_data[NUM_LINE_BUFFERS / 2][(MAX_FRAME_WIDTH * 3) / 2];
    uint32_t line_lengths[NUM_LINE_BUFFERS];
    bool h_double[NUM_LINE_BUFFERS];

    Sprite sprites[MAX_SPRITES];

    // Not using this yet but keeping it here to remind us of the memory usage!
    uint32_t tmds_palette_luts[32 * 32 * 12];
    uint32_t* tmds_15bpp_lut = &tmds_palette_luts[32 * 32 * 2];

    // TMDS buffers.  Better to have them here than rely on dynamic allocation
    uint32_t tmds_buffers[NUM_TMDS_BUFFERS * 3 * MAX_FRAME_WIDTH / DVI_SYMBOLS_PER_WORD];

    Diags diags;

    // Whether the RAM should be in SPI mode for the app processor
    bool spi_mode = false;

    // Whether to output heartbeat LED
    bool heartbet_led = true;
};