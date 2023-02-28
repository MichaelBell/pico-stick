#include <cstring>
#include <cstdio>
#include "frame_decode.hpp"

using namespace pico_stick;

namespace {
    constexpr int headers_len_in_bytes = (4 + sizeof(Config) + sizeof(FrameTableHeader));
    constexpr int headers_len_in_words = headers_len_in_bytes / 4;

}

bool FrameDecode::read_headers() {
    uint32_t buffer[headers_len_in_words];

    ram.read_blocking(0, buffer, headers_len_in_words);

    if (buffer[0] != 0x4F434950) {
        // Magic word wrong.
        printf("Magic word should be 0x4F434950, got %08lx\n", buffer[0]);
        return false;
    }

    memcpy(&config, buffer + 1, sizeof(Config));
    memcpy(&frame_table_header, buffer + 1 + sizeof(Config) / 4, sizeof(FrameTableHeader));

    return true;
}

void FrameDecode::get_frame_table(int frame_counter, FrameTableEntry* frame_table) {
    uint32_t address = get_frame_table_address() + frame_counter * frame_table_header.frame_table_length;

    ram.read_blocking(address, (uint32_t*)frame_table, frame_table_header.frame_table_length);
}

void FrameDecode::get_palette(int idx, int frame_counter, uint8_t palette[256 * 3]) {
    uint32_t address = get_palette_table_address() + (idx + frame_table_header.num_palettes * (frame_table_header.palette_advance ? frame_counter : 0)) * 256 * 3;

    ram.read(address, (uint32_t*)palette, (256 * 3) / 4);
}

void FrameDecode::get_sprite_header(int idx, pico_stick::SpriteHeader* sprite_header) {
    uint32_t address = get_sprite_table_address() + idx * 4;

    ram.read_blocking(address, (uint32_t*)sprite_header, 1);

    uint32_t header_data;
    ram.read_blocking(sprite_header->sprite_address(), &header_data, 1);
    uint8_t* header_ptr = (uint8_t*)&header_data;
    sprite_header->width = header_ptr[0];
    sprite_header->height = header_ptr[1];
}

void FrameDecode::get_sprite(int idx, const pico_stick::SpriteHeader& sprite_header, pico_stick::SpriteLine* sprite_line_table, uint32_t* sprite_data) {
    uint32_t address = sprite_header.sprite_address();

    assert(sprite_header.height <= MAX_SPRITE_HEIGHT);
    ram.read_blocking(address, buffer, (sprite_header.height >> 1) + 1);

    uint16_t total_length = 0;
    uint8_t* ptr = (uint8_t*)buffer + 2;
    for (uint8_t y = 0; y < sprite_header.height; ++y) {
        sprite_line_table[y].data_start = total_length;
        sprite_line_table[y].offset = *ptr++;
        sprite_line_table[y].width = *ptr++;
        total_length += sprite_line_table[y].width * get_pixel_data_len(sprite_header.sprite_mode());
    }

    address += 4 + 4 * (sprite_header.height >> 1);
    ram.read(address, sprite_data, (total_length + 3) >> 2);
}

uint32_t FrameDecode::get_frame_table_address() {
    return headers_len_in_bytes;
}

uint32_t FrameDecode::get_palette_table_address() {
    return headers_len_in_bytes + frame_table_header.num_frames * frame_table_header.frame_table_length * 4;
}

uint32_t FrameDecode::get_sprite_table_address() {
    return get_palette_table_address() + frame_table_header.num_palettes * (frame_table_header.palette_advance ? frame_table_header.num_frames : 1) * 256 * 3;
}
