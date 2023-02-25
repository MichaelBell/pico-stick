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
        MODE_ARGB1555 = 1,  // 2 bytes per pixel: Alpha 15, Red 14-10, Green 9-5, Blue 4-0
        MODE_PALETTE = 2,   // 1 byte per pixel: Colour 6-2, Alpha 0 (unused bits must be zero), maps to RGB888 palette entry, 32 colour palette (not yet implemented)
        MODE_RGB888 = 3,    // 3 bytes per pixel R, G, B (not yet implemented)
        MODE_INVALID = 0xFF
    };

    enum BlendMode : uint8_t {
        BLEND_NONE = 0,     // Sprite replaces frame
        BLEND_DEPTH = 1,    // Depth order, back to front: Sprite A0, Frame A0, Sprite A1, Frame A1
        BLEND_DEPTH2 = 2,   // Depth order, back to front: Sprite A0, Frame A0, Frame A1, Sprite A1
        BLEND_BLEND = 3,    // Use frame if Sprite A0 or Frame A1, add if Sprite A1 and Frame A0
        BLEND_BLEND2 = 4,   // Use frame if Sprite A0, add if Sprite A1
    };

    struct Config {
        Resolution res;
        uint8_t unused;
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

        bool apply_frame_offset() const { return (entry >> 31) != 0; }
        LineMode line_mode() const { return LineMode((entry >> 28) & 0x7); }
        uint32_t h_repeat() const { return (entry >> 24) & 0xF; }
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
        case MODE_ARGB1555:
            return 2;

        case MODE_RGB888:
            return 3;

        case MODE_PALETTE:
            return 1;
        }
    }
}