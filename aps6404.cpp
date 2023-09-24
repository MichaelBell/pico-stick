#include <algorithm>
#include "aps6404.hpp"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "aps6404.pio.h"

namespace pimoroni {
    APS6404::APS6404(uint pin_csn, uint pin_d0, PIO pio)
                : pin_csn(pin_csn)
                , pin_d0(pin_d0)
                , pio(pio)
    {
        // Initialize data pins
        for (int i = 0; i < 4; ++i) {
            gpio_init(pin_d0 + i);
            gpio_disable_pulls(pin_d0 + i);
        }

        pio_prog = &sram_reset_program;
        pio_offset = pio_add_program(pio, &sram_reset_program);
        pio_sm = pio_claim_unused_sm(pio, true);

        // Claim DMA channels
        dma_channel = dma_claim_unused_channel(true);
        read_cmd_dma_channel = dma_claim_unused_channel(true);
        setup_dma_config();
    }

    void APS6404::init() {
        aps6404_reset_program_init(pio, pio_sm, pio_offset, pin_csn, pin_d0);

        sleep_us(200);
        pio_sm_put_blocking(pio, pio_sm, 0x00000007u);
        pio_sm_put_blocking(pio, pio_sm, 0x66000000u);
        pio_sm_put_blocking(pio, pio_sm, 0x00000007u);
        pio_sm_put_blocking(pio, pio_sm, 0x99000000u);
        pio_sm_put_blocking(pio, pio_sm, 0x00000007u);
        pio_sm_put_blocking(pio, pio_sm, 0x35000000u);
        sleep_us(500);

        adjust_clock();
    }

    void APS6404::set_qpi() {
        pio_sm_set_enabled(pio, pio_sm, false);
        pio_remove_program(pio, pio_prog, pio_offset);

        pio_prog = &sram_reset_program;
        pio_offset = pio_add_program(pio, &sram_reset_program);
        aps6404_reset_program_init(pio, pio_sm, pio_offset, pin_csn, pin_d0);
        pio_sm_put_blocking(pio, pio_sm, 0x00000007u);
        pio_sm_put_blocking(pio, pio_sm, 0x35000000u);

        while (!pio_sm_is_tx_fifo_empty(pio, pio_sm) || pio->sm[pio_sm].addr != pio_offset);

        adjust_clock();
    }

    void APS6404::set_spi() {
        pio_sm_set_enabled(pio, pio_sm, false);
        pio_remove_program(pio, pio_prog, pio_offset);

        pio_prog = &sram_reset_qpi_program;
        pio_offset = pio_add_program(pio, &sram_reset_qpi_program);
        aps6404_program_init(pio, pio_sm, pio_offset, pin_csn, pin_d0, false, false, true);
        pio_sm_put_blocking(pio, pio_sm, 0x00000001u);
        pio_sm_put_blocking(pio, pio_sm, 0xF5000000u);
    }

    void APS6404::adjust_clock() {
        pio_sm_set_enabled(pio, pio_sm, false);
        pio_remove_program(pio, pio_prog, pio_offset);

        if (clock_get_hz(clk_sys) > 296000000) {
            pio_prog = &sram_fast_program;
            pio_offset = pio_add_program(pio, &sram_fast_program);
            aps6404_program_init(pio, pio_sm, pio_offset, pin_csn, pin_d0, false, true, false);
        }
        else if (clock_get_hz(clk_sys) < 130000000) {
            pio_prog = &sram_slow_program;
            pio_offset = pio_add_program(pio, &sram_slow_program);
            aps6404_program_init(pio, pio_sm, pio_offset, pin_csn, pin_d0, true, false, false);
        }
        else {
            pio_prog = &sram_program;
            pio_offset = pio_add_program(pio, &sram_program);
            aps6404_program_init(pio, pio_sm, pio_offset, pin_csn, pin_d0, false, false, false);
        }
    }

    void APS6404::write(uint32_t addr, uint32_t* data, uint32_t len_in_words) {
        wait_for_finish_blocking();

        for (int len = len_in_words, page_len = std::min((PAGE_SIZE - (addr & (PAGE_SIZE - 1))) >> 2, len_in_words); 
             len > 0; 
             addr += page_len << 2, data += page_len, len -= page_len, page_len = std::min(PAGE_SIZE >> 2, len))
        {
            wait_for_finish_blocking();

            pio_sm_put_blocking(pio, pio_sm, (page_len * 8) - 1);
            pio_sm_put_blocking(pio, pio_sm, 0x38000000u | addr);
            pio_sm_put_blocking(pio, pio_sm, pio_offset + sram_offset_do_write);

            dma_channel_configure(
                dma_channel, &write_config,
                &pio->txf[pio_sm],
                data,
                page_len,
                true
            );
        }
    }

    void APS6404::read(uint32_t addr, uint32_t* read_buf, uint32_t len_in_words) {
        start_read(read_buf, len_in_words);

        uint32_t first_page_len = (PAGE_SIZE - (addr & (PAGE_SIZE - 1)));

        if (first_page_len >= len_in_words << 2) {
            pio_sm_put_blocking(pio, pio_sm, (len_in_words * 8) - 4);
            pio_sm_put_blocking(pio, pio_sm, 0xeb000000u | addr);
            pio_sm_put_blocking(pio, pio_sm, pio_offset + sram_offset_do_read);
        }
        else {
            uint32_t* cmd_buf = add_read_to_cmd_buffer(multi_read_cmd_buffer, addr, len_in_words << 2);
            dma_channel_transfer_from_buffer_now(read_cmd_dma_channel, multi_read_cmd_buffer, cmd_buf - multi_read_cmd_buffer);
        }
    }

    void APS6404::multi_read(uint32_t* addresses, uint32_t* lengths, uint32_t num_reads, uint32_t* read_buf, int chain_channel) {
        uint32_t total_len = 0;
        uint32_t* cmd_buf = multi_read_cmd_buffer;
        for (uint32_t i = 0; i < num_reads; ++i) {
            total_len += lengths[i];
            cmd_buf = add_read_to_cmd_buffer(cmd_buf, addresses[i], lengths[i]);
        }

        start_read(read_buf, total_len >> 2, chain_channel);

        dma_channel_transfer_from_buffer_now(read_cmd_dma_channel, multi_read_cmd_buffer, cmd_buf - multi_read_cmd_buffer);
    }

    void APS6404::start_read(uint32_t* read_buf, uint32_t total_len_in_words, int chain_channel) {
        wait_for_finish_blocking();

        dma_channel_config c = read_config;
        if (chain_channel >= 0) {
            channel_config_set_chain_to(&c, chain_channel);
        }
        
        dma_channel_configure(
            dma_channel, &c,
            read_buf,
            &pio->rxf[pio_sm],
            total_len_in_words,
            true
        );
    }

    void APS6404::setup_dma_config() {
        dma_channel_config c = dma_channel_get_default_config(read_cmd_dma_channel);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(pio, pio_sm, true));
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        
        dma_channel_configure(
            read_cmd_dma_channel, &c,
            &pio->txf[pio_sm],
            multi_read_cmd_buffer,
            0,
            false
        );

        write_config = dma_channel_get_default_config(dma_channel);
        channel_config_set_read_increment(&write_config, true);
        channel_config_set_write_increment(&write_config, false);
        channel_config_set_dreq(&write_config, pio_get_dreq(pio, pio_sm, true));
        channel_config_set_transfer_data_size(&write_config, DMA_SIZE_32);
        channel_config_set_bswap(&write_config, true);

        read_config = dma_channel_get_default_config(dma_channel);
        channel_config_set_read_increment(&read_config, false);
        channel_config_set_write_increment(&read_config, true);
        channel_config_set_dreq(&read_config, pio_get_dreq(pio, pio_sm, false));
        channel_config_set_transfer_data_size(&read_config, DMA_SIZE_32);
        channel_config_set_bswap(&read_config, true);        
    }

    uint32_t* APS6404::add_read_to_cmd_buffer(uint32_t* cmd_buf, uint32_t addr, uint32_t len_in_bytes) {
        int32_t len_remaining = len_in_bytes;
        uint32_t len = std::min((PAGE_SIZE - (addr & (PAGE_SIZE - 1))), (uint32_t)len_remaining);

        while (true) {
            if (len < 2) {
                *cmd_buf++ = 0;
                *cmd_buf++ = 0xeb000000u | addr;
                *cmd_buf++ = pio_offset + sram_offset_do_read_one;
            }
            else {
                *cmd_buf++ = (len * 2) - 4;
                *cmd_buf++ = 0xeb000000u | addr;
                *cmd_buf++ = pio_offset + sram_offset_do_read;
            }
            len_remaining -= len;
            addr += len;

            if (len_remaining <= 0) break;

            len = len_remaining;
            if (len > PAGE_SIZE) len = PAGE_SIZE;
        }

        return cmd_buf;
    }

    void APS6404::wait_for_finish_blocking() {
        dma_channel_wait_for_finish_blocking(dma_channel);
    }
}
