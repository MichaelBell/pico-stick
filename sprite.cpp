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
    if (idx < 0) return;

    for (int i = 0; i < header.height; ++i) {
        int line_idx = y + i*v_scale;
        if (line_idx < 0 || line_idx >= disp.frame_data.config.v_length) continue;
        auto& line = lines[i];
        if (line.width == 0) continue;
        
        int start = x + line.offset;
        int end = start + line.width;
        int start_offset = 0;
        int line_len = disp.frame_data.config.h_length;
        if (disp.frame_table[line_idx].h_repeat() == 2) line_len >>= 1;

        if (end <= 0) continue;
        if (start >= line_len) continue;
        if (end > line_len) end = line_len;
        
        const int pixel_size = get_pixel_data_len(header.sprite_mode());
        start *= pixel_size;
        end *= pixel_size;
        if (start < 0) {
            start_offset = -start;
            start = 0;
        }

        const int len = end - start;
        uint8_t* const sprite_data_ptr = data + line.data_start + start_offset;

        for (uint8_t i = 0; i < v_scale && line_idx < disp.frame_data.config.v_length; ++i) {
            auto* patch = disp.patches[line_idx++];
            int j = 0;
            for (; patch->data && j < MAX_PATCHES_PER_LINE; ++j) {
                ++patch;
            }
            if (j == MAX_PATCHES_PER_LINE) {
                continue;
            }
            patch->data = sprite_data_ptr;
            patch->offset = start;
            patch->len = len;
            patch->mode = blend_mode;
        }
    }
}

__scratch_x("sprite_buffer") int Sprite::dma_channel_x;
__scratch_x("sprite_buffer") uint32_t Sprite::buffer_x[MAX_SPRITE_WIDTH / 2];
__scratch_y("sprite_buffer") int Sprite::dma_channel_y;
__scratch_y("sprite_buffer") uint32_t Sprite::buffer_y[MAX_SPRITE_WIDTH / 2];

__always_inline static void blend_one_555(BlendMode mode, uint16_t* sprite_pixel_ptr, uint16_t* frame_pixel_ptr) {
    constexpr uint16_t alpha_mask = 0x8000;
    constexpr uint16_t blend_mask = 0x7BDE;
    switch (mode) {
        case BLEND_DEPTH:
        {
            if ((*sprite_pixel_ptr & ~*frame_pixel_ptr) & alpha_mask) {
                *frame_pixel_ptr = *sprite_pixel_ptr & (alpha_mask - 1);
            }
            break;
        }
        case BLEND_DEPTH2:
        {
            if (*sprite_pixel_ptr & alpha_mask) {
                *frame_pixel_ptr = *sprite_pixel_ptr;
            }
            break;
        }
        case BLEND_BLEND:
        {
            if ((*sprite_pixel_ptr & ~*frame_pixel_ptr) & alpha_mask) {
                *frame_pixel_ptr = (uint32_t(*frame_pixel_ptr & blend_mask) + uint32_t(*sprite_pixel_ptr & blend_mask)) >> 1;
            }
            break;
        }
        case BLEND_BLEND2:
        {
            if (*sprite_pixel_ptr & alpha_mask) {
                *frame_pixel_ptr = ((uint32_t(*frame_pixel_ptr & blend_mask) + uint32_t(*sprite_pixel_ptr & blend_mask)) >> 1) | (*frame_pixel_ptr & alpha_mask);
            }
            break;
        }
        default:
        {
            *frame_pixel_ptr = *sprite_pixel_ptr;
        }
    }
}

__always_inline static void apply_blend_patch_555(const Sprite::BlendPatch& patch, uint8_t* frame_pixel_data, uint32_t* sprite_buffer, int dma_channel) {
    uint16_t* sprite_pixel_ptr = (uint16_t*)patch.data;
    uint16_t* const sprite_end_ptr = (uint16_t*)(patch.data + patch.len);
    uint16_t* frame_pixel_ptr = (uint16_t*)((uint8_t*)frame_pixel_data + patch.offset);

    // Align sprite_pixel_ptr
    if ((uintptr_t)sprite_pixel_ptr & 3) {
        blend_one_555(patch.mode, sprite_pixel_ptr++, frame_pixel_ptr++);
    }

    uint32_t* sprite_pixel_ptr32 = (uint32_t*)sprite_pixel_ptr;
    uint32_t* const sprite_end_ptr32 = (uint32_t*)((uintptr_t)sprite_end_ptr & ~3);
    uint32_t* frame_pixel_ptr32;
    bool dma_reqd;
    if (((uintptr_t)frame_pixel_ptr & 3) && sprite_end_ptr32 > sprite_pixel_ptr32) {
        dma_channel_wait_for_finish_blocking(dma_channel);
        frame_pixel_ptr32 = sprite_buffer;
        dma_channel_set_read_addr(dma_channel, frame_pixel_ptr, false);
        dma_channel_transfer_to_buffer_now(dma_channel, sprite_buffer, (sprite_end_ptr32 - sprite_pixel_ptr32) << 1);
        dma_reqd = true;
    }
    else {
        frame_pixel_ptr32 = (uint32_t*)frame_pixel_ptr;
        dma_reqd = false;
    }

    // Final pixel
    if ((uintptr_t)sprite_end_ptr & 3) {
        blend_one_555(patch.mode, sprite_end_ptr - 1, (uint16_t*)((uint8_t*)frame_pixel_data + patch.offset) + (sprite_end_ptr - 1 - (uint16_t*)patch.data));
    }

    if (sprite_pixel_ptr32 >= sprite_end_ptr32) {
        return;
    }

    constexpr uint32_t alpha_mask = 0x80008000; 
    constexpr uint32_t blend_mask = 0x7BDE7BDE;
    switch (patch.mode) {
        case BLEND_DEPTH:
        {
            for (; sprite_pixel_ptr32 < sprite_end_ptr32; ++sprite_pixel_ptr32, ++frame_pixel_ptr32) {
                uint32_t mask = (*sprite_pixel_ptr32 & ~*frame_pixel_ptr32) & alpha_mask;
                mask = mask - (mask >> 15);
                *frame_pixel_ptr32 = (*frame_pixel_ptr32 & ~mask) | (*sprite_pixel_ptr32 & mask);
            }
            break;
        }
        case BLEND_DEPTH2:
        {
            for (; sprite_pixel_ptr32 < sprite_end_ptr32; ++sprite_pixel_ptr32, ++frame_pixel_ptr32) {
                uint32_t mask = *sprite_pixel_ptr32 & alpha_mask;
                mask = (mask >> 15) * 0xFFFF;
                *frame_pixel_ptr32 = (*frame_pixel_ptr32 & ~mask) | (*sprite_pixel_ptr32 & mask);
            }
            break;
        }
        case BLEND_BLEND:
        {
            // This is the most expensive case, and the compiler's asm is fairly poor (at least on gcc 9.2.1)
            // so we have some inline assembler.
#if 0
            for (; sprite_pixel_ptr32 < sprite_end_ptr32; ++sprite_pixel_ptr32, ++frame_pixel_ptr32) {
                uint32_t mask = (*sprite_pixel_ptr32 & ~*frame_pixel_ptr32) & alpha_mask;
                mask = mask - (mask >> 15);
                uint32_t blended = (((*frame_pixel_ptr32) & blend_mask) + ((*sprite_pixel_ptr32) & blend_mask)) >> 1;
                *frame_pixel_ptr32 = (*frame_pixel_ptr32 & ~mask) | (blended & mask);
            }
#else
                asm ( ".align 2\n\t"
                      "1: ldmia %[sprite_pixel_ptr]!, {r1}\n\t"
                      "ldr r2, [%[frame_pixel_ptr]]\n\t"
                      "movs r3, r1\n\t"
                      "bic r3, r2\n\t"
                      "and r1, %[blend_mask]\n\t"
                      "and r3, %[alpha_mask]\n\t"
                      "lsr r0, r3, #15\n\t"
                      "sub r3, r3, r0\n\t"
                      "movs r0, r2\n\t"
                      "and r0, %[blend_mask]\n\t"
                      "add r0, r0, r1\n\t"
                      "lsr r0, r0, #1\n\t"
                      "bic r2, r3\n\t"
                      "and r0, r3\n\t"
                      "orr r2, r0\n\t"
                      "stmia %[frame_pixel_ptr]!, {r2}\n\t"
                      "cmp %[sprite_pixel_ptr], %[sprite_end_ptr]\n\t"
                      "bcc 1b\n\t" :
                      [frame_pixel_ptr] "+l" (frame_pixel_ptr32),
                      [sprite_pixel_ptr] "+l" (sprite_pixel_ptr32) :
                      [alpha_mask] "l" (alpha_mask),
                      [blend_mask] "l" (blend_mask),
                      [sprite_end_ptr] "r" (sprite_end_ptr32) :
                      "r1",  // sprite_pixel
                      "r2",  // frame_pixel
                      "r0", "r3", "cc" );
#endif
            break;
        }
        case BLEND_BLEND2:
        {
#if 0
            for (; sprite_pixel_ptr32 < sprite_end_ptr32; ++sprite_pixel_ptr32, ++frame_pixel_ptr32) {
                uint32_t mask = *sprite_pixel_ptr32 & alpha_mask;
                mask = mask - (mask >> 15);
                uint32_t blended = (((*frame_pixel_ptr32) & blend_mask) + ((*sprite_pixel_ptr32) & blend_mask)) >> 1;
                *frame_pixel_ptr32 = (*frame_pixel_ptr32 & ~mask) | (blended & mask);
            }
#else
                asm ( ".align 2\n\t"
                      "2: ldmia %[sprite_pixel_ptr]!, {r1}\n\t"
                      "ldr r2, [%[frame_pixel_ptr]]\n\t"
                      "movs r3, r1\n\t"
                      "and r1, %[blend_mask]\n\t"
                      "and r3, %[alpha_mask]\n\t"
                      "lsr r0, r3, #15\n\t"
                      "sub r3, r3, r0\n\t"
                      "movs r0, r2\n\t"
                      "and r0, %[blend_mask]\n\t"
                      "add r0, r0, r1\n\t"
                      "lsr r0, r0, #1\n\t"
                      "bic r2, r3\n\t"
                      "and r0, r3\n\t"
                      "orr r2, r0\n\t"
                      "stmia %[frame_pixel_ptr]!, {r2}\n\t"
                      "cmp %[sprite_pixel_ptr], %[sprite_end_ptr]\n\t"
                      "bcc 2b\n\t" :
                      [frame_pixel_ptr] "+l" (frame_pixel_ptr32),
                      [sprite_pixel_ptr] "+l" (sprite_pixel_ptr32) :
                      [alpha_mask] "l" (alpha_mask),
                      [blend_mask] "l" (blend_mask),
                      [sprite_end_ptr] "r" (sprite_end_ptr32) :
                      "r1",  // sprite_pixel
                      "r2",  // frame_pixel
                      "r0", "r3", "cc" );
#endif
            break;
        }
        default:
        {
            for (; sprite_pixel_ptr32 < sprite_end_ptr32; ++sprite_pixel_ptr32, ++frame_pixel_ptr32) {
                *frame_pixel_ptr32 = *sprite_pixel_ptr32;
            }
        }
    }

    if (dma_reqd) {
        // DMA doing halfword transfers to fix up the misalignment.
        dma_channel_set_read_addr(dma_channel, sprite_buffer, false);
        dma_channel_transfer_to_buffer_now(dma_channel, frame_pixel_ptr, (frame_pixel_ptr32 - sprite_buffer) << 1);
    }
}

__always_inline static void apply_blend_patch_byte(const Sprite::BlendPatch& patch, uint8_t* frame_pixel_data, uint32_t* sprite_buffer, int dma_channel) {
    uint8_t* sprite_pixel_ptr = (uint8_t*)patch.data;
    uint8_t* const sprite_end_ptr = (uint8_t*)(patch.data + patch.len);
    uint8_t* frame_pixel_ptr = ((uint8_t*)frame_pixel_data + patch.offset);
    constexpr uint8_t alpha_mask = 0x01;

    switch (patch.mode) {
        case BLEND_DEPTH:
        case BLEND_BLEND:
            for (; sprite_pixel_ptr < sprite_end_ptr; ++sprite_pixel_ptr, ++frame_pixel_ptr) {
                if ((*sprite_pixel_ptr & ~*frame_pixel_ptr) & alpha_mask) {
                    *frame_pixel_ptr = *sprite_pixel_ptr & (~alpha_mask);
                }
            }
            break;
        case BLEND_DEPTH2:
        case BLEND_BLEND2:
            for (; sprite_pixel_ptr < sprite_end_ptr; ++sprite_pixel_ptr, ++frame_pixel_ptr) {
                if (*sprite_pixel_ptr & alpha_mask) {
                    *frame_pixel_ptr = *sprite_pixel_ptr;
                }
            }
            break;
        default:
            while (sprite_pixel_ptr < sprite_end_ptr) {
                *frame_pixel_ptr++ = *sprite_pixel_ptr++;
            }
            break;
    }
}

void __scratch_x("sprite_blend") Sprite::apply_blend_patch_555_x(const BlendPatch& patch, uint8_t* frame_pixel_data) {
    apply_blend_patch_555(patch, frame_pixel_data, buffer_x, dma_channel_x);
}

void __scratch_y("sprite_blend") Sprite::apply_blend_patch_555_y(const BlendPatch& patch, uint8_t* frame_pixel_data) {
    apply_blend_patch_555(patch, frame_pixel_data, buffer_y, dma_channel_y);
}

void __scratch_x("sprite_blend") Sprite::apply_blend_patch_byte_x(const BlendPatch& patch, uint8_t* frame_pixel_data) {
    apply_blend_patch_byte(patch, frame_pixel_data, buffer_x, dma_channel_x);
}

void __scratch_y("sprite_blend") Sprite::apply_blend_patch_byte_y(const BlendPatch& patch, uint8_t* frame_pixel_data) {
    apply_blend_patch_byte(patch, frame_pixel_data, buffer_y, dma_channel_y);
}

void Sprite::init() {
    // Claim DMA channels
    dma_channel_x = dma_claim_unused_channel(true);
    dma_channel_y = dma_claim_unused_channel(true);

    // Setup Sprite copying DMA channels - transfer halfwords from memory to memory
    dma_channel_config c;
    c = dma_channel_get_default_config(dma_channel_x);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    dma_channel_configure(
        dma_channel_x, &c,
        nullptr,
        nullptr,
        0,
        false
    );

    c = dma_channel_get_default_config(dma_channel_y);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, true);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    dma_channel_configure(
        dma_channel_y, &c,
        nullptr,
        nullptr,
        0,
        false
    );
}