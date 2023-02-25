#include <cstring>
#include <cstdio>

#include "sprite.hpp"
#include "display.hpp"

using namespace pico_stick;

void Sprite::update_sprite(FrameDecode& frame_data) {
    if (idx < 0) return;

    frame_data.get_sprite_header(idx, &header);

    //printf("Setup sprite width %d, height %d\n", header.width, header.height);
    frame_data.get_sprite(idx, header, lines, (uint32_t*)data);
}

void Sprite::setup_patches(DisplayDriver& disp) {
    for (int i = 0; i < header.height; ++i) {
        const int line_idx = y + i;
        if (line_idx < 0 || line_idx >= disp.frame_data.config.v_length) continue;
        auto& line = lines[i];
        if (line.width == 0) continue;
        
        int start = x + line.offset;
        int end = start + line.width;
        int start_offset = 0;
        if (end <= 0) continue;
        if (start >= disp.frame_data.config.h_length) continue;
        if (end > disp.frame_data.config.h_length) end = disp.frame_data.config.h_length;
        
        const int pixel_size = get_pixel_data_len(header.sprite_mode());
        start *= pixel_size;
        end *= pixel_size;
        if (start < 0) {
            start_offset = -start;
            start = 0;
        }

        const int len = end - start;
        uint8_t* const sprite_data_ptr = data + line.data_start + start_offset;
        if (blend_mode != BLEND_NONE) {
            auto* patch = disp.blend_patches[line_idx];
            for (int j = 0; patch->data && j < MAX_BLEND_PATCHES_PER_LINE - 1; ++j) {
                ++patch;
            }
            patch->data = sprite_data_ptr;
            patch->offset = start;
            patch->len = len;
            patch->mode = blend_mode;
        }
        else {
            auto* patch = disp.patches[line_idx];
            for (int j = 0; patch->data && j < MAX_PATCHES_PER_LINE - 1; ++j) {
                ++patch;
            }
            patch->data = sprite_data_ptr;

            uint8_t* pixel_data_ptr = (uint8_t*)disp.pixel_data[(line_idx >> 1) & 1];
            if (line_idx & 1) {
                pixel_data_ptr += get_pixel_data_len(disp.frame_table[line_idx].line_mode()) * disp.frame_data.config.h_length;
            }

            patch->dest_ptr = pixel_data_ptr + start;
            patch->len = len;
        }
    }
}

void Sprite::apply_blend_patch(const BlendPatch& patch, uint8_t* frame_pixel_data) {
    uint16_t* sprite_pixel_ptr = (uint16_t*)patch.data;
    uint16_t* frame_pixel_ptr = (uint16_t*)((uint8_t*)frame_pixel_data + patch.offset);

    constexpr uint16_t alpha_mask = 1;
    switch (patch.mode) {
        // TODO: Should be able to do two pixels at a time with no branching using bit ops,
        // can generate masks quickly from the alpha by multiplying by 0xFFFE (RP2040 has single cycle multiply)
        case BLEND_DEPTH:
        {
            for (int j = 0; j < (patch.len >> 1); ++j) {
                if ((sprite_pixel_ptr[j] & alpha_mask) && !(frame_pixel_ptr[j] & alpha_mask)) {
                    frame_pixel_ptr[j] = sprite_pixel_ptr[j];
                }
            }
            break;
        }
        case BLEND_DEPTH2:
        {
            for (int j = 0; j < (patch.len >> 1); ++j) {
                if (sprite_pixel_ptr[j] & alpha_mask) {
                    frame_pixel_ptr[j] = sprite_pixel_ptr[j];
                }
            }
            break;
        }
        case BLEND_BLEND:
        {
            for (int j = 0; j < (patch.len >> 1); ++j) {
                if ((sprite_pixel_ptr[j] & alpha_mask) && !(frame_pixel_ptr[j] & alpha_mask)) {
                    frame_pixel_ptr[j] = (uint32_t(frame_pixel_ptr[j] & 0xF7BC) + uint32_t(sprite_pixel_ptr[j] & 0xF7BC)) >> 1;
                }
            }
            break;
        }
        case BLEND_BLEND2:
        {
            for (int j = 0; j < (patch.len >> 1); ++j) {
                if (sprite_pixel_ptr[j] & alpha_mask) {
                    frame_pixel_ptr[j] = (uint32_t(frame_pixel_ptr[j] & 0xF7BC) + uint32_t(sprite_pixel_ptr[j] & 0xF7BC)) >> 1;
                }
            }
            break;
        }
        default:
            break;
    }
}