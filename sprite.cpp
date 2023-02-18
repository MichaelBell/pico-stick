#include <cstring>
#include <cstdio>

#include "sprite.hpp"

using namespace pico_stick;

void Sprite::update_sprite(FrameDecode& frame_data, LinePatch patch_array[MAX_FRAME_HEIGHT][MAX_PATCHES_PER_LINE]) {
    if (idx < 0) return;

    frame_data.get_sprite_header(idx, &header);

    //printf("Setup sprite width %d, height %d\n", header.width, header.height);
    lines.resize(header.height);
    data.resize((header.height * header.width * get_pixel_data_len(header.sprite_mode()) + 3) >> 2);
    frame_data.get_sprite(idx, header, lines.data(), data.data());

    for (int i = 0; i < header.height; ++i) {
        const int line_idx = y + i;
        auto& line = lines[i];
        auto* patch = patch_array[line_idx];
        if (patch->data) {
            ++patch;
        }

        int start = x + line.offset;
        int end = start + line.width;
        if (end <= 0) continue;
        if (start >= frame_data.config.h_length) continue;
        if (start < 0) start = 0;
        if (end >= frame_data.config.h_length) end = frame_data.config.h_length;
        
        const int pixel_size = get_pixel_data_len(header.sprite_mode());
        start *= pixel_size;
        end *= pixel_size;

        patch->mode = header.sprite_mode();
        patch->offset = start;
        patch->data = (uint8_t*)data.data() + line.data_start;
        patch->len = end - start;
    }
}
