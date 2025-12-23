/**
 * M5Dial Hello World Sample
 *
 * Features:
 * - Display "Hello World" on LCD
 * - Rotate encoder to change counter
 * - Press encoder button to reset counter
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

static const char *TAG = "M5Dial-Hello";

// LCD Pin Definitions
#define LCD_MOSI_PIN 5
#define LCD_SCLK_PIN 6
#define LCD_DC_PIN   4
#define LCD_CS_PIN   7
#define LCD_RST_PIN  8
#define LCD_BL_PIN   9

// Encoder Pin Definitions
#define ENCODER_A_PIN 41
#define ENCODER_B_PIN 40
#define ENCODER_BTN_PIN 42

// M5Dial Display Class
class LGFX_M5Dial : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_M5Dial(void) {
        // SPI Bus Configuration
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI3_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 80000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = true;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = LCD_SCLK_PIN;
            cfg.pin_mosi = LCD_MOSI_PIN;
            cfg.pin_miso = -1;
            cfg.pin_dc = LCD_DC_PIN;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // Panel Configuration
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = LCD_CS_PIN;
            cfg.pin_rst = LCD_RST_PIN;
            cfg.pin_busy = -1;
            cfg.panel_width = 240;
            cfg.panel_height = 240;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;

            _panel_instance.config(cfg);
        }

        // Backlight Configuration
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = LCD_BL_PIN;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;

            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

// Global instances
LGFX_M5Dial display;
int32_t counter = 0;
int32_t last_encoder_value = 0;

// Encoder interrupt variables
volatile int32_t encoder_count = 0;

// Encoder ISR
static void IRAM_ATTR encoder_a_isr(void* arg) {
    int b_state = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
    if (b_state == 0) {
        encoder_count++;
    } else {
        encoder_count--;
    }
}

// Button ISR
static void IRAM_ATTR button_isr(void* arg) {
    counter = 0;
    encoder_count = 0;
}

// Initialize Encoder
void encoder_init() {
    // Configure encoder pins as input with pull-up
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ENCODER_A_PIN) | (1ULL << ENCODER_B_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // Configure button pin
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << ENCODER_BTN_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // Install GPIO ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)ENCODER_A_PIN, encoder_a_isr, NULL);
    gpio_isr_handler_add((gpio_num_t)ENCODER_BTN_PIN, button_isr, NULL);

    ESP_LOGI(TAG, "Encoder initialized");
}

// Update Display
void update_display() {
    display.fillScreen(TFT_BLACK);

    // Draw title
    display.setTextColor(TFT_WHITE);
    display.setTextDatum(MC_DATUM);
    display.setFont(&fonts::FreeSansBold18pt7b);
    display.drawString("Hello World", 120, 60);

    // Draw counter label
    display.setFont(&fonts::FreeSans12pt7b);
    display.drawString("Counter:", 120, 120);

    // Draw counter value
    display.setFont(&fonts::FreeSansBold24pt7b);
    display.setTextColor(TFT_CYAN);
    display.drawNumber(counter, 120, 160);

    // Draw instruction
    display.setFont(&fonts::Font0);
    display.setTextColor(TFT_LIGHTGREY);
    display.drawString("Rotate: Change", 120, 200);
    display.drawString("Press: Reset", 120, 215);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "M5Dial Hello World Starting...");

    // Initialize Display
    display.init();
    display.setBrightness(128);
    display.setRotation(0);

    ESP_LOGI(TAG, "Display initialized");

    // Initialize Encoder
    encoder_init();

    // Initial display
    update_display();

    ESP_LOGI(TAG, "Entering main loop");

    // Main loop
    while (1) {
        // Check if encoder value changed
        int32_t current_encoder = encoder_count;
        if (current_encoder != last_encoder_value) {
            counter = current_encoder;
            last_encoder_value = current_encoder;
            update_display();
            ESP_LOGI(TAG, "Counter: %ld", counter);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
