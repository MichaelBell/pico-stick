#pragma once

#include <vector>
#include "constants.hpp"
#include "frame_decode.hpp"

class Sprite {
    public:
        void set_sprite_table_idx(int16_t table_idx) {
            idx = table_idx;
        }

        bool is_enabled() const { return idx >= 0; }

        int16_t get_sprite_table_idx() const { return idx; }

        void set_sprite_pos(int16_t new_x, int16_t new_y) {
            x = new_x; y = new_y;
        }

        void set_blend_mode(pico_stick::BlendMode mode) {
            blend_mode = mode;
        }

        void set_sprite_v_scale(uint8_t new_v_scale) {
            v_scale = new_v_scale;
        }

        pico_stick::BlendMode get_blend_mode() const {
            return blend_mode;
        }

        struct LinePatch {
            uint8_t* data;
            uint8_t* dest_ptr;
            uint32_t len;     // in bytes
            uint32_t ctrl;    // Control word for DMA chain
        };

        struct BlendPatch {
            uint8_t* data;
            uint16_t offset; // in bytes
            uint8_t len;     // in bytes
            pico_stick::BlendMode mode;
        };

        void update_sprite(FrameDecode& frame_data);
        void copy_sprite(const Sprite& other);
        void setup_patches(class DisplayDriver& disp);
        static void apply_blend_patch_555_x(const BlendPatch& patch, uint8_t* frame_pixel_data);
        static void apply_blend_patch_555_y(const BlendPatch& patch, uint8_t* frame_pixel_data);
        static void apply_blend_patch_byte_x(const BlendPatch& patch, uint8_t* frame_pixel_data);
        static void apply_blend_patch_byte_y(const BlendPatch& patch, uint8_t* frame_pixel_data);

        static void init();
        static void clear_sprite_data();

    private:
        int16_t x;
        int16_t y;
        int16_t idx = -1;
        uint8_t v_scale = 1;
        pico_stick::BlendMode blend_mode = pico_stick::BLEND_NONE;

        pico_stick::SpriteHeader header;
        pico_stick::SpriteLine lines[MAX_SPRITE_HEIGHT];
        uint8_t* data = nullptr;

        static int dma_channel_x;
        static int dma_channel_y;
        static uint32_t buffer_x[MAX_SPRITE_WIDTH / 2];
        static uint32_t buffer_y[MAX_SPRITE_WIDTH / 2];
};