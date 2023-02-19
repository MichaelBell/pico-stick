#include <cstring>
#include <cstdio>

#include "sprite.hpp"
#include "display.hpp"

using namespace pico_stick;

void Sprite::update_sprite(FrameDecode& frame_data) {
    if (idx < 0) return;

    frame_data.get_sprite_header(idx, &header);

    //printf("Setup sprite width %d, height %d\n", header.width, header.height);
    lines.resize(header.height);
    data.resize((header.height * header.width * get_pixel_data_len(header.sprite_mode()) + 3) >> 2);
    frame_data.get_sprite(idx, header, lines.data(), data.data());
}

namespace {
    uint32_t tmp_frame_data[MAX_SPRITE_WIDTH / 2];
}

void Sprite::setup_patches(DisplayDriver& disp) {
    uint32_t* tmp_frame_data_ptr = tmp_frame_data;

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
        uint8_t* const sprite_data_ptr = (uint8_t*)data.data() + line.data_start + start_offset;
        if (blend_mode != BLEND_NONE) {
            // TODO: Consider non RGB565/RGAB5515
            
            // TODO could stream this better
            disp.ram.read_blocking(disp.get_line_address(line_idx) + (start & ~3), tmp_frame_data_ptr, (len + 6) >> 2);

            switch (header.sprite_mode()) {
                case MODE_RGB565:
                {
                    switch (blend_mode) {
                        case BLEND_BLEND:
                        case BLEND_BLEND2:
                        {
                            uint16_t* sprite_pixel_ptr = (uint16_t*)sprite_data_ptr;
                            uint16_t* frame_pixel_ptr = (uint16_t*)((uint8_t*)tmp_frame_data_ptr + (start & 3));
                            for (int j = 0; j < (len >> 1); ++j) {
                                sprite_pixel_ptr[j] = (uint32_t(frame_pixel_ptr[j] & 0xF7DF) + uint32_t(sprite_pixel_ptr[j] & 0xF7DF)) >> 1;
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }
                case MODE_RGAB5515:
                {
                    constexpr uint16_t alpha_mask = (1 << 5);
                    switch (blend_mode) {
                        case BLEND_DEPTH:
                        case BLEND_DEPTH2:
                        {
                            uint16_t* sprite_pixel_ptr = (uint16_t*)sprite_data_ptr;
                            uint16_t* frame_pixel_ptr = (uint16_t*)((uint8_t*)tmp_frame_data_ptr + (start & 3));
                            for (int j = 0; j < (len >> 1); ++j) {
                                if (!(sprite_pixel_ptr[j] & alpha_mask)) {
                                    sprite_pixel_ptr[j] = frame_pixel_ptr[j];
                                }
                            }
                            break;
                        }
                        case BLEND_BLEND:
                        case BLEND_BLEND2:
                        {
                            uint16_t* sprite_pixel_ptr = (uint16_t*)sprite_data_ptr;
                            uint16_t* frame_pixel_ptr = (uint16_t*)((uint8_t*)tmp_frame_data_ptr + (start & 3));
                            for (int j = 0; j < (len >> 1); ++j) {
                                if (sprite_pixel_ptr[j] & alpha_mask) {
                                    sprite_pixel_ptr[j] = (uint32_t(frame_pixel_ptr[j] & 0xF7DF) + uint32_t(sprite_pixel_ptr[j] & 0xF7DF)) >> 1;
                                }
                                else {
                                    sprite_pixel_ptr[j] = frame_pixel_ptr[j];
                                }
                            }
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        auto* patch = disp.patches[line_idx];
        uint32_t lock = spin_lock_blocking(disp.patch_lock);
        for (int j = 0; patch->data && j < MAX_PATCHES_PER_LINE - 1; ++j) {
            ++patch;
        }
        patch->data = sprite_data_ptr;
        spin_unlock(disp.patch_lock, lock);

        uint8_t* pixel_data_ptr = (uint8_t*)disp.pixel_data[(line_idx >> 1) & 1];
        if (line_idx & 1) {
            pixel_data_ptr += get_pixel_data_len(disp.frame_table[line_idx].line_mode()) * disp.frame_data.config.h_length;
        }

        patch->dest_ptr = pixel_data_ptr + start;
        patch->len = len;
    }
}
