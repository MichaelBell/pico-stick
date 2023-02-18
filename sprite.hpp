#pragma once

#include <vector>
#include "constants.hpp"
#include "frame_decode.hpp"

class Sprite {
    public:
        Sprite()
            : idx(-1)
        {}

        void set_sprite_table_idx(int16_t table_idx) {
            idx = table_idx;
        }

        bool is_enabled() const { return idx >= 0; }

        uint16_t get_sprite_table_idx() const { return idx; }

        void set_sprite_pos(int16_t new_x, int16_t new_y) {
            x = new_x; y = new_y;
        }

        struct LinePatch {
            uint8_t* data;
            uint16_t offset;  // in bytes
            uint8_t len;     // in bytes
            pico_stick::LineMode mode;
        };

        void update_sprite(FrameDecode& frame_data, LinePatch patch_array[MAX_FRAME_HEIGHT][MAX_PATCHES_PER_LINE]);

    private:
        int16_t x;
        int16_t y;
        int16_t idx;

        pico_stick::SpriteHeader header;
        std::vector<pico_stick::SpriteLine> lines;
        std::vector<uint32_t> data;
};