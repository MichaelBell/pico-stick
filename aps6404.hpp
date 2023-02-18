#pragma once

#include <stdint.h>
#include "hardware/pio.h"

namespace pimoroni {
    class APS6404 {
        public:
            static constexpr int RAM_SIZE = 8 * 1024 * 1024;
            static constexpr int PAGE_SIZE = 1024;

            APS6404(uint pin_csn = 17, uint pin_d0 = 19, PIO pio = pio0)
                : pin_csn(pin_csn)
                , pin_d0(pin_d0)
                , pio(pio)
            {}

            void init();

            // Start a write, this completes asynchronously, this function blocks if another 
            // transfer is already in progress
            // Writes should not cross page boundaries when running faster than 84MHz
            // Writes should always be <= 1KB.
            void write(uint32_t addr, uint32_t* data, uint32_t len_in_words);

            // Start a read, this completes asynchronously, this function only blocks if another 
            // transfer is already in progress
            void read(uint32_t addr, uint32_t* read_buf, uint32_t len_in_words);

            // Start multiple reads to the same buffer.  They completes asynchronously, 
            // this function only blocks if another transfer is already in progress
            void multi_read(uint32_t* addresses, uint32_t* lengths, uint32_t num_addresses, uint32_t* read_buf, int chain_channel = -1);

            // Read and block until completion
            void read_blocking(uint32_t addr, uint32_t* read_buf, uint32_t len_in_words) {
                read(addr, read_buf, len_in_words);
                wait_for_finish_blocking();
            }

            // Block until any outstanding read or write completes
            void wait_for_finish_blocking();

        private:
            void start_read(uint32_t* read_buf, uint32_t total_len_in_words, int chain_channel = -1);
            void setup_cmd_buffer_dma(bool clear = false);
            uint32_t* add_read_to_cmd_buffer(uint32_t* cmd_buf, uint32_t addr, uint32_t len_in_words);

            uint pin_csn;  // CSn, SCK must be next pin after CSn
            uint pin_d0;   // D0, D1, D2, D3 must be consecutive

            PIO pio;
            uint pio_sm;
            uint pio_offset;

            uint dma_channel;
            uint read_cmd_dma_channel;

            static constexpr int MULTI_READ_MAX_PAGES = 128;
            uint32_t multi_read_cmd_buffer[3 * MULTI_READ_MAX_PAGES];
    };
}
