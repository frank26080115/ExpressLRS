#if (defined(GPIO_PIN_PWM_OUTPUTS) && defined(PLATFORM_ESP32) && defined(PLATFORM_ESP32_C3))

#pragma once

#include "DShotRMT.h"

class DShotRMT {
    public:
        DShotRMT(gpio_num_t gpio, int idx);
        ~DShotRMT();

        // ...safety first ...no parameters, no DShot
        bool begin(dshot_mode_t dshot_mode = DSHOT_OFF, bool is_bidirectional = false);
        void set_looping(bool);
        void send_dshot_value(uint16_t throttle_value, telemetric_request_t telemetric_request = NO_TELEMETRIC);

        static void poll(); // call frequently; poll waits for the complete frame before switching GPIO

    private:
        gpio_num_t gpio_num;
        int my_idx;
        void* next_node = NULL; // linked list next node
    
        dshot_packet_t next_packet; // stores the data to be sent when the turn comes
        uint32_t last_send_time = 0;
    
        dshot_mode_t mode = DSHOT_OFF;
        bool bidirectional = false;
        uint16_t ticks_per_bit = 0;
        uint16_t ticks_zero_high = 0;
        uint16_t ticks_zero_low = 0;
        uint16_t ticks_one_high = 0;
        uint16_t ticks_one_low = 0;

        uint8_t has_new_data = 0;
        bool looping = true;

        rmt_item32_t* encode_dshot_to_rmt(uint16_t parsed_packet);
        uint16_t calc_dshot_chksum(const dshot_packet_t& dshot_packet);
        uint16_t prepare_rmt_data(const dshot_packet_t& dshot_packet);
        void output_rmt_data();
        void set_pin();
};

#endif
