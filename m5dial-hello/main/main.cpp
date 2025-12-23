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
#include "driver/ledc.h"
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

// Buzzer Pin Definition
#define BUZZER_PIN 3

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
LGFX_Sprite canvas(&display);  // Off-screen buffer
int32_t counter = 0;
int32_t last_encoder_value = 0;

// Encoder interrupt variables
volatile int32_t encoder_count = 0;
volatile int32_t encoder_raw = 0;
static int8_t last_state = 0;

// Quadrature state table: [last_state][current_state] -> direction (-1, 0, +1)
// States: 00=0, 01=1, 11=2, 10=3 (Gray code order)
static const int8_t quad_table[4][4] = {
    // to:  0   1   2   3   from:
    {  0, +1,  0, -1 },  // 0
    { -1,  0, +1,  0 },  // 1
    {  0, -1,  0, +1 },  // 2
    { +1,  0, -1,  0 },  // 3
};

// Encoder ISR - Quadrature decoding
static void IRAM_ATTR encoder_isr(void* arg) {
    int a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
    int b = gpio_get_level((gpio_num_t)ENCODER_B_PIN);

    // Convert to Gray code state (0,1,3,2 -> 0,1,2,3)
    int8_t state = (a << 1) | (a ^ b);

    // Get direction from state transition
    int8_t dir = quad_table[last_state][state];
    encoder_raw += dir;
    last_state = state;

    // Convert raw count to detent count (4 pulses per detent)
    encoder_count = encoder_raw / 4;
}

// Buzzer request flags (set in ISR, processed in main loop)
volatile bool buzzer_click_request = false;
volatile bool buzzer_reset_request = false;

// Button ISR
static void IRAM_ATTR button_isr(void* arg) {
    counter = 0;
    encoder_count = 0;
    encoder_raw = 0;
    buzzer_reset_request = true;
}

// Initialize Buzzer
void buzzer_init() {
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 4000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .gpio_num = BUZZER_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&channel_conf);

    ESP_LOGI(TAG, "Buzzer initialized");
}

// Play a short beep
void buzzer_beep(uint32_t freq, uint32_t duration_ms) {
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);  // 50% duty
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// Initialize Encoder
void encoder_init() {
    // Configure encoder pins as input with pull-up, trigger on any edge
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ENCODER_A_PIN) | (1ULL << ENCODER_B_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // Configure button pin
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << ENCODER_BTN_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // Initialize last state
    int a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
    int b = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
    last_state = (a << 1) | (a ^ b);

    // Install GPIO ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)ENCODER_A_PIN, encoder_isr, NULL);
    gpio_isr_handler_add((gpio_num_t)ENCODER_B_PIN, encoder_isr, NULL);
    gpio_isr_handler_add((gpio_num_t)ENCODER_BTN_PIN, button_isr, NULL);

    ESP_LOGI(TAG, "Encoder initialized");
}

// Update Display (using sprite for flicker-free rendering)
void update_display() {
    canvas.fillScreen(TFT_BLACK);

    // Draw title
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.drawString("Hello World", 120, 60);

    // Draw counter label
    canvas.setFont(&fonts::FreeSans12pt7b);
    canvas.drawString("Counter:", 120, 120);

    // Draw counter value
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextColor(TFT_CYAN);
    canvas.drawNumber(counter, 120, 160);

    // Draw instruction
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(TFT_LIGHTGREY);
    canvas.drawString("Rotate: Change", 120, 200);
    canvas.drawString("Press: Reset", 120, 215);

    // Push to display in one go
    canvas.pushSprite(0, 0);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "M5Dial Hello World Starting...");

    // Initialize Display
    display.init();
    display.setBrightness(128);
    display.setRotation(0);

    // Create sprite buffer (240x240, 16-bit color)
    canvas.createSprite(240, 240);

    ESP_LOGI(TAG, "Display initialized");

    // Initialize Buzzer
    buzzer_init();

    // Initialize Encoder
    encoder_init();

    // Initial display
    update_display();

    // Startup sound
    buzzer_beep(2000, 50);

    ESP_LOGI(TAG, "Entering main loop");

    // Main loop
    while (1) {
        // Check for buzzer requests
        if (buzzer_reset_request) {
            buzzer_reset_request = false;
            buzzer_beep(1000, 100);  // Lower tone for reset
            update_display();
        }

        // Check if encoder value changed
        int32_t current_encoder = encoder_count;
        if (current_encoder != last_encoder_value) {
            counter = current_encoder;
            last_encoder_value = current_encoder;
            update_display();
            buzzer_beep(4000, 10);  // Short click sound
            ESP_LOGI(TAG, "Counter: %ld", counter);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
