#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)

#include "PWM_ESP32_OneShot.h"

PwmOneShot *PwmOneShot::head = nullptr;
PwmOneShot *PwmOneShot::tail = nullptr;
PwmOneShot *PwmOneShot::current = nullptr;
rmt_channel_t PwmOneShot::rmtChannel = RMT_CHANNEL_MAX;
int PwmOneShot::previousPin = -1;
uint32_t PwmOneShot::lastStartUs = 0;
uint16_t PwmOneShot::lastDurationUs = 0;

static void releaseRmtPin(int pin)
{
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << pin;
    config.mode = GPIO_MODE_OUTPUT;
    gpio_config(&config);
    gpio_set_level((gpio_num_t)pin, 0);
}

PwmOneShot::PwmOneShot(gpio_num_t gpio, uint16_t frequency, eServoOutputFailsafeMode mode)
    : pin(gpio), periodUs(1000000U / frequency), failsafeMode(mode) {}

bool PwmOneShot::initialized()
{
    return head != nullptr;
}

bool PwmOneShot::begin(rmt_channel_t channel)
{
    if (registered) {
        return true;
    }
    if (!initialized()) {
        rmt_config_t config = {};
        config.rmt_mode = RMT_MODE_TX;
        config.channel = channel;
        config.gpio_num = pin;
        config.clk_div = APB_CLK_FREQ / 1000000U; // One RMT tick per microsecond.
        config.mem_block_num = 1;
        config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
        config.tx_config.idle_output_en = true;
        config.tx_config.loop_en = false;
        config.tx_config.carrier_en = false;
        if (rmt_config(&config) != ESP_OK || rmt_driver_install(channel, 0, 0) != ESP_OK) {
            return false;
        }
        rmtChannel = channel;
        previousPin = pin;
        lastDurationUs = 0;
    }

    if (head == nullptr) {
        head = tail = current = this;
        next = this;
    } else {
        next = head;
        tail->next = this;
        tail = this;
    }
    registered = true;
    return true;
}

PwmOneShot::~PwmOneShot()
{
    if (!registered) {
        return;
    }
    if (head == this && tail == this) {
        rmt_tx_stop(rmtChannel);
        rmt_driver_uninstall(rmtChannel);
        if (previousPin >= 0) {
            releaseRmtPin(previousPin);
        }
        head = tail = current = nullptr;
        rmtChannel = RMT_CHANNEL_MAX;
        previousPin = -1;
        return;
    }

    PwmOneShot *prior = head;
    while (prior->next != this) {
        prior = prior->next;
    }
    prior->next = next;
    if (head == this) head = next;
    if (tail == this) tail = prior;
    if (current == this) current = next;
    if (previousPin == pin) {
        rmt_tx_stop(rmtChannel);
        releaseRmtPin(pin);
        previousPin = -1;
        lastDurationUs = 0;
    }
}

void PwmOneShot::setPulse(uint16_t microseconds)
{
    pulseUs = microseconds > 32767U ? 32767U : microseconds; // RMT duration fields are 15 bits.
    if (microseconds == 0) {
        // A no-pulse failsafe must also stop a pulse already in progress.
        if (registered && previousPin == pin) {
            rmt_tx_stop(rmtChannel);
            releaseRmtPin(pin);
            previousPin = -1;
            lastDurationUs = 0;
        }
    }
}

void PwmOneShot::poll(bool rfConnected)
{
    if (current == nullptr) {
        return;
    }
    const uint32_t nowUs = micros();
    if (lastDurationUs && (uint32_t)(nowUs - lastStartUs) < lastDurationUs + 10U) {
        return;
    }
    rmt_channel_status_result_t status;
    if (rmt_get_channel_status(&status) != ESP_OK || status.status[rmtChannel] != RMT_CHANNEL_IDLE) {
        return;
    }

    PwmOneShot *const start = current;
    PwmOneShot *candidate = start;
    do {
        current = candidate->next;
        if (candidate->pulseUs != 0 &&
            (rfConnected || candidate->failsafeMode != PWMFAILSAFE_NO_PULSES) &&
            (!candidate->hasSent || (uint32_t)(nowUs - candidate->lastPulseUs) >= candidate->periodUs)) {
            if (previousPin >= 0 && previousPin != candidate->pin) {
                releaseRmtPin(previousPin);
                previousPin = -1;
            }
            if (previousPin != candidate->pin) {
                pinMode(candidate->pin, OUTPUT);
                if (rmt_set_gpio(rmtChannel, RMT_MODE_TX, candidate->pin, false) != ESP_OK) {
                    return;
                }
                previousPin = candidate->pin;
            }

            rmt_item32_t items[2] = {};
            items[0].level0 = 1;
            items[0].duration0 = candidate->pulseUs;
            items[0].level1 = 0;
            items[0].duration1 = 1;
            if (rmt_fill_tx_items(rmtChannel, items, 2, 0) == ESP_OK &&
                rmt_tx_start(rmtChannel, true) == ESP_OK) {
                candidate->lastPulseUs = nowUs;
                candidate->hasSent = true;
                lastStartUs = nowUs;
                lastDurationUs = candidate->pulseUs + 1;
            }
            return; // At most one pulse per call.
        }
        candidate = current;
    } while (candidate != start);
}

#endif
