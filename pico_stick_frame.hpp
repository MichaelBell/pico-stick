#pragma once

#include <cstdint>

namespace pico_stick {
    enum Resolution : uint8_t {
        RESOLUTION_OFF = 0,
        RESOLUTION_640x480 = 1,
        RESOLUTION_720x576 = 2,
        RESOLUTION_800x480 = 3,
        RESOLUTION_800x600 = 4,
    };

    enum LineMode : uint8_t {
        MODE_RGB565 = 0,    // 2 bytes per pixel: Red: 15-11, Green 10-5, Blue 4-0
        MODE_ARGB1555 = 1,  // 2 bytes per pixel: Alpha: 15, Red: 14-10, Green: 9-5, Blue 4-0
        MODE_RGB888 = 2,    // 3 bytes per pixel R, G, B.
        MODE_PALETTE8 = 3,  // 1 byte per pixel: Maps to ARGB8888 palette entry
        MODE_INVALID = 0xFF
    };

    struct Config {
        Resolution res;
        uint8_t h_repeat;
        uint8_t v_repeat;
        bool blank;

        uint16_t h_offset;
        uint16_t h_length;
        uint16_t v_offset;
        uint16_t v_length;
    };

    struct FrameTableHeader {
        uint16_t num_frames;
        uint16_t first_frame;
        uint16_t frame_table_length;
        uint8_t frame_rate_divider;
        uint8_t bank_number;
        uint8_t num_palettes;
        bool palette_advance;
        uint16_t num_sprites;
    };

    struct FrameTableEntry {
        uint32_t entry;

        uint32_t apply_frame_offset() const { return entry >> 31; }
        LineMode line_mode() const { return LineMode((entry >> 28) & 0x7); }
        uint32_t palette_index() const { return (entry >> 24) & 0xF; }
        uint32_t line_address() const { return entry & 0xFFFFFF; }
    };

    struct SpriteHeader {
        uint32_t hdr;
        LineMode sprite_mode() const { return LineMode(hdr >> 28); }
        uint32_t palette_index() const { return (hdr >> 24) & 0xF; }
        uint32_t sprite_address() const { return hdr & 0xFFFFFF; }

        uint8_t width;
        uint8_t height;
    };

    struct SpriteLine {
        uint8_t offset;
        uint8_t width;
        uint16_t data_start;  // Index into data of start of line
    };

    inline uint32_t get_pixel_data_len(pico_stick::LineMode mode) {
        switch (mode)
        {
        default:
        case MODE_RGB565:
        case MODE_ARGB1555:
            return 2;

        case MODE_RGB888:
            return 3;

        case MODE_PALETTE8:
            return 1;
        }
    }
}