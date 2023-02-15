#include "aps6404.hpp"
extern "C" {
#include "dvi.h"
#include "dvi_timing.h"
}
#include "frame_decode.hpp"

class DisplayDriver {
    public:
        static constexpr int PIN_RAM_CS = 2;
        static constexpr int PIN_RAM_D0 = 4;
        static constexpr int PIN_VSYNC = 22;
        static constexpr int PIN_HEARTBEAT = 25;

        static constexpr int PIN_HDMI_CLK = 14;
        static constexpr int PIN_HDMI_D0 = 12;
        static constexpr int PIN_HDMI_D1 = 18;
        static constexpr int PIN_HDMI_D2 = 16;

        DisplayDriver(PIO pio=pio1)
            : frame_data(ram)
            , current_res(pico_stick::RESOLUTION_640x480)
            , ram(PIN_RAM_CS, PIN_RAM_D0)
            , dvi0 { 
                .timing{ &dvi_timing_640x480p_60hz },
                .ser_cfg{ 
                    .pio = pio,
                    .sm_tmds = {0, 1, 2},
                    .pins_tmds = {PIN_HDMI_D0, PIN_HDMI_D1, PIN_HDMI_D2},
                    .pins_clk = PIN_HDMI_CLK,
                    .invert_diffpairs = false                
                }
            }
        {}

        void init();

        // Runs the display.  This never returns and also starts processing on core1.
        // The resolution and hence timing are specified in the PSRAM
        // so this sets up/resets the DVI appropriately as they change.
        void run();

        // Called internally by run().
        void run_core1();

        pimoroni::APS6404& get_ram() { return ram; }

    private:
        void read_two_lines(uint idx);

        FrameDecode frame_data;
        pico_stick::Resolution current_res;

        pimoroni::APS6404 ram;
        struct dvi_inst dvi0;

        int frame_counter = 0;
        int line_counter = 0;

        // Must be as long as the greatest supported frame height.
        pico_stick::FrameTableEntry frame_table[600];

        // Must be long enough to accept two lines at maximum data length and maximum width
        uint32_t pixel_data[2][(800 * 3) / 2];
        uint32_t line_lengths[4];
};