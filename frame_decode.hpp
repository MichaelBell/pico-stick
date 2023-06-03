#pragma once

#include "pico_stick_frame.hpp"
#include "constants.hpp"
#include "aps6404.hpp"

class FrameDecode {
    public:    
        static constexpr int MAX_SPRITE_HEIGHT = 64;

        FrameDecode(pimoroni::APS6404& aps6404)
            : ram(aps6404)
        {}

        // Read the headers from PSRAM.  Returns false if PSRAM contents is invalid
        bool read_headers();

        // Fill the frame table from PSRAM, frame_table is an array of at least config.v_length
        void get_frame_table(int frame_counter, pico_stick::FrameTableEntry* frame_table);

        // Fill a palette
        void get_palette(int idx, int frame_counter, uint8_t palette[PALETTE_SIZE * 3]);

        // Get a sprite header
        void get_sprite_header(int idx, pico_stick::SpriteHeader* sprite_header);
        
        // Fill a sprite into appropriately sized buffer
        void get_sprite(int idx, const pico_stick::SpriteHeader& sprite_header, pico_stick::SpriteLine* sprite_line_table, uint32_t* sprite_data);

    public:
        pico_stick::Config config;
        pico_stick::FrameTableHeader frame_table_header;

    private:
        uint32_t get_frame_table_address();
        uint32_t get_palette_table_address();
        uint32_t get_sprite_table_address();

        pimoroni::APS6404& ram;
        uint32_t buffer[(MAX_SPRITE_HEIGHT >> 1) + 1];
};
