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
    static constexpr int PIN_RAM_CS = 2;
    static constexpr int PIN_RAM_D0 = 4;
    static constexpr int PIN_VSYNC = 22;
    static constexpr int PIN_HEARTBEAT = 25;

    static constexpr int PIN_HDMI_CLK = 14;
    static constexpr int PIN_HDMI_D0 = 12;
    static constexpr int PIN_HDMI_D1 = 18;
    static constexpr int PIN_HDMI_D2 = 16;

    DisplayDriver(PIO pio = pio1)
        : frame_data(ram), current_res(pico_stick::RESOLUTION_640x480), ram(PIN_RAM_CS, PIN_RAM_D0), dvi0{
                                                                                                         .timing{&dvi_timing_640x480p_60hz},
                                                                                                         .ser_cfg{
                                                                                                             .pio = pio,
                                                                                                             .sm_tmds = {0, 1, 2},
                                                                                                             .pins_tmds = {PIN_HDMI_D0, PIN_HDMI_D1, PIN_HDMI_D2},
                                                                                                             .pins_clk = PIN_HDMI_CLK,
                                                                                                             .invert_diffpairs = false}}
    {
    }

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

private:
    friend class Sprite;

    void main_loop();
    void prepare_scanline_core0(int line_number, uint32_t *pixel_data, uint32_t *tmds_buf);
    void prepare_scanline_core1(int line_number, uint32_t *pixel_data, uint32_t *tmds_buf);
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
    pico_stick::FrameTableEntry frame_table[MAX_FRAME_HEIGHT];

    // Patches that require blending, done by CPU
    Sprite::BlendPatch patches[MAX_FRAME_HEIGHT][MAX_PATCHES_PER_LINE];

    // Must be long enough to accept two lines at maximum data length and maximum width
    uint32_t pixel_data[NUM_LINE_BUFFERS / 2][(MAX_FRAME_WIDTH * 3) / 2];
    uint32_t line_lengths[NUM_LINE_BUFFERS];

    Sprite sprites[MAX_SPRITES];

    // Not using this yet but keeping it here to remind us of the memory usage!
    uint32_t tmds_palette_luts[32 * 32 * 12];
    uint32_t* tmds_15bpp_lut = &tmds_palette_luts[32 * 32 * 2];
};