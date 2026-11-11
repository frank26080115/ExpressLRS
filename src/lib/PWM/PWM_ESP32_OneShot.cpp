#if defined(PLATFORM_ESP32) && defined(BUILD_SHREW_PWM_ONESHOT)

#include "PWM_ESP32_OneShot.h"

PwmOneShot *PwmOneShot::head = nullptr;
PwmOneShot *PwmOneShot::tail = nullptr;
PwmOneShot *PwmOneShot::current = nullptr;
rmt_channel_t PwmOneShot::rmtChannel = RMT_CHANNEL_MAX;
int PwmOneShot::previousPin = -1;
rmt_item32_t PwmOneShot::txItems[2] = {};

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
        // Let an in-flight item finish off-pin and release its driver semaphore.
        releaseRmtPin(pin);
        previousPin = -1;
    }
}

void PwmOneShot::setPulse(uint16_t microseconds)
{
    // Zero stops scheduling this output. Leave the current pulse intact;
    // poll() disconnects the GPIO after TX completion. A positive update
    // before then cancels the stop without reconnecting an active waveform.
    pulseUs = microseconds > 32767U ? 32767U : microseconds; // RMT duration fields are 15 bits.
}

void PwmOneShot::poll(bool rfConnected)
{
    if (current == nullptr) {
        return;
    }
    // Pair the driver's write API with its completion semaphore. Low-level
    // rmt_tx_start() does not acquire it, making driver status appear idle
    // during transmission. Never switch GPIOs or rewrite items until TX_END.
    if (rmt_wait_tx_done(rmtChannel, 0) != ESP_OK) {
        return;
    }
    const uint32_t nowUs = micros();

    PwmOneShot *const start = current;
    PwmOneShot *candidate = start;
    do {
        current = candidate->next;
        if (previousPin == candidate->pin && candidate->pulseUs == 0) {
            releaseRmtPin(previousPin);
            previousPin = -1;
        }
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

            // Keep the buffer alive and unchanged until the asynchronous write
            // completes. This channel has one producer; the completion check
            // above guarantees rmt_write_items() can take its semaphore now.
            txItems[0].level0 = 1;
            txItems[0].duration0 = candidate->pulseUs;
            txItems[0].level1 = 0;
            txItems[0].duration1 = 1;
            if (rmt_write_items(rmtChannel, txItems, 2, false) == ESP_OK) {
                candidate->lastPulseUs = micros();
                candidate->hasSent = true;
            }
            return; // At most one pulse per call.
        }
        candidate = current;
    } while (candidate != start);
}

#endif
