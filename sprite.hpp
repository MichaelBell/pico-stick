#pragma once

#include <vector>
#include "frame_decode.hpp"

class Sprite {
    public:
        Sprite()
            : enabled(0)
        {}

        void set_enabled(bool enable) {
            enabled = enable ? -1 : 0;
        }

        bool is_enabled() { return enabled != 0; }

        void set_sprite_pos(int16_t new_x, int16_t new_y) {
            x = new_x; y = new_y;
        }

        struct LinePatch {
            pico_stick::LineMode mode;
            uint8_t* data;
            uint32_t offset;  // in bytes
            uint32_t len;     // in bytes
        };

        void update_sprite(int idx, FrameDecode& frame_data, LinePatch* patch_array);

    private:
        int16_t x;
        int16_t enabled : 1;
        int16_t y : 15;

        pico_stick::SpriteHeader header;
        std::vector<pico_stick::SpriteLine> lines;
        std::vector<uint32_t> data;
};