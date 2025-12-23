/**
 * M5Dial WS2812B LED Controller
 *
 * Controls:
 * - Rotate encoder: Adjust value (hue/brightness/LED count depending on mode)
 * - Short press: Change mode
 * - Long press: Toggle LED on/off
 *
 * Modes:
 * 1. Hue - Change color
 * 2. Saturation - Change saturation
 * 3. Brightness - Change brightness
 * 4. LED Count - How many LEDs to light up
 * 5. Effect - Select animation effect
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "led_strip.h"
#include "esp_random.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "wifi_credentials.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

static const char *TAG = "M5Dial-LED";

// Pin Definitions
#define LCD_MOSI_PIN 5
#define LCD_SCLK_PIN 6
#define LCD_DC_PIN   4
#define LCD_CS_PIN   7
#define LCD_RST_PIN  8
#define LCD_BL_PIN   9

#define ENCODER_A_PIN 41
#define ENCODER_B_PIN 40
#define ENCODER_BTN_PIN 42

#define BUZZER_PIN 3

// WS2812B Configuration
#define LED_STRIP_PIN GPIO_NUM_15  // Grove Port A - GPIO15 (white wire / SCL)
#define LED_STRIP_MAX_LEDS 60      // Maximum supported LEDs

// M5Dial Display Class
class LGFX_M5Dial : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_M5Dial(void) {
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
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = LCD_CS_PIN;
            cfg.pin_rst = LCD_RST_PIN;
            cfg.pin_busy = -1;
            cfg.panel_width = 240;
            cfg.panel_height = 240;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.invert = true;
            cfg.rgb_order = false;
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }
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
LGFX_Sprite canvas(&display);
led_strip_handle_t led_strip = NULL;

// LED State
uint16_t led_hue = 0;         // 0-359
uint8_t led_saturation = 255; // 0-255
uint8_t led_brightness = 128; // 0-255
uint8_t led_count = 10;       // How many LEDs to light
uint8_t led_effect = 0;       // Current effect
bool led_on = true;           // LED on/off

// Effect names (Japanese)
const char* effect_names[] = {
    "単色",
    "虹色",
    "呼吸",
    "追跡",
    "キラキラ"
};
#define NUM_EFFECTS 5

// Control modes
enum ControlMode {
    MODE_HUE = 0,
    MODE_SATURATION,
    MODE_BRIGHTNESS,
    MODE_COUNT,
    MODE_EFFECT,
    MODE_MAX
};

// Mode names (Japanese)
const char* mode_names[] = {
    "色相",
    "彩度",
    "明るさ",
    "LED数",
    "エフェクト"
};

ControlMode current_mode = MODE_HUE;

// Encoder state
volatile int32_t encoder_count = 0;
static int8_t last_state = 0;

// Button state
static bool last_button_state = false;
static uint32_t button_press_time = 0;
static bool button_was_long_press = false;
static bool button_just_released_short = false;
static const uint32_t LONG_PRESS_MS = 300;

// WiFi state
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static char ip_address[16] = "";
static bool ota_in_progress = false;
static int ota_progress = 0;

// ===== HSV to RGB Conversion =====

void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }

    uint8_t region = h / 60;
    uint8_t remainder = (h - (region * 60)) * 255 / 60;

    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

// ===== LED Strip Functions =====

void led_strip_init() {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_PIN,
        .max_leds = LED_STRIP_MAX_LEDS,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = { .invert_out = false }
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false }
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED strip init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Clear LEDs on init
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);
}

void update_leds() {
    if (!led_on) {
        led_strip_clear(led_strip);
        led_strip_refresh(led_strip);
        return;
    }

    static uint32_t effect_counter = 0;
    effect_counter++;

    uint8_t r, g, b;

    switch (led_effect) {
        case 0: // Solid color
            hsv_to_rgb(led_hue, led_saturation, led_brightness, &r, &g, &b);
            for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                if (i < led_count) {
                    led_strip_set_pixel(led_strip, i, r, g, b);
                } else {
                    led_strip_set_pixel(led_strip, i, 0, 0, 0);
                }
            }
            break;

        case 1: // Rainbow
            for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                if (i < led_count) {
                    uint16_t hue = (led_hue + (i * 360 / led_count) + effect_counter) % 360;
                    hsv_to_rgb(hue, led_saturation, led_brightness, &r, &g, &b);
                    led_strip_set_pixel(led_strip, i, r, g, b);
                } else {
                    led_strip_set_pixel(led_strip, i, 0, 0, 0);
                }
            }
            break;

        case 2: // Breathing
            {
                float breath = (sin(effect_counter * 0.05) + 1.0) / 2.0;
                uint8_t bright = (uint8_t)(led_brightness * breath);
                hsv_to_rgb(led_hue, led_saturation, bright, &r, &g, &b);
                for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                    if (i < led_count) {
                        led_strip_set_pixel(led_strip, i, r, g, b);
                    } else {
                        led_strip_set_pixel(led_strip, i, 0, 0, 0);
                    }
                }
            }
            break;

        case 3: // Chase
            {
                int chase_pos = (effect_counter / 3) % led_count;
                hsv_to_rgb(led_hue, led_saturation, led_brightness, &r, &g, &b);
                for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                    if (i < led_count) {
                        if (i == chase_pos) {
                            led_strip_set_pixel(led_strip, i, r, g, b);
                        } else {
                            led_strip_set_pixel(led_strip, i, r/10, g/10, b/10);
                        }
                    } else {
                        led_strip_set_pixel(led_strip, i, 0, 0, 0);
                    }
                }
            }
            break;

        case 4: // Sparkle
            hsv_to_rgb(led_hue, led_saturation, led_brightness, &r, &g, &b);
            for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                if (i < led_count) {
                    if (esp_random() % 20 == 0) {
                        led_strip_set_pixel(led_strip, i, r, g, b);
                    } else {
                        led_strip_set_pixel(led_strip, i, r/8, g/8, b/8);
                    }
                } else {
                    led_strip_set_pixel(led_strip, i, 0, 0, 0);
                }
            }
            break;
    }

    led_strip_refresh(led_strip);
}

// ===== Encoder ISR =====
// Count only on detent (1 click = 1 step)
// Direction determined by final transition into state 00

static void IRAM_ATTR encoder_isr(void *arg) {
    uint8_t a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
    uint8_t b = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
    int8_t state = (a << 1) | b;

    // Only count when returning to detent (00)
    // Direction is determined by which state we came from
    if (state == 0b00) {
        if (last_state == 0b10) {
            encoder_count++;   // Clockwise: 10 -> 00
        } else if (last_state == 0b01) {
            encoder_count--;   // Counter-clockwise: 01 -> 00
        }
    }

    last_state = state;
}

// ===== Button Handling =====

void update_button_state() {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool current_button = (gpio_get_level((gpio_num_t)ENCODER_BTN_PIN) == 0);
    button_just_released_short = false;

    if (current_button && !last_button_state) {
        button_press_time = now;
        button_was_long_press = false;
    } else if (current_button && last_button_state) {
        if (!button_was_long_press && (now - button_press_time) >= LONG_PRESS_MS) {
            button_was_long_press = true;
        }
    } else if (!current_button && last_button_state) {
        if (!button_was_long_press) {
            button_just_released_short = true;
        }
    }
    last_button_state = current_button;
}

bool is_button_held() {
    return last_button_state && button_was_long_press;
}

bool was_short_press() {
    return button_just_released_short;
}

// ===== Buzzer =====

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
}

void buzzer_beep(uint32_t freq, uint32_t duration_ms) {
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ===== Display =====

// Simple UI colors
#define UI_BLACK   0x0000
#define UI_WHITE   0xFFFF
#define UI_GRAY    0x8410

// UI state: false = Layer 1 (menu select), true = Layer 2 (value adjust)
static bool in_adjustment_mode = false;

// Circle parameters
#define CIRCLE_CENTER_X  120
#define CIRCLE_CENTER_Y  120
#define CIRCLE_RADIUS    90
#define DOT_RADIUS_SMALL 5
#define DOT_RADIUS_LARGE 8

// Draw a dot at angle position on the circle
void draw_circle_dot(float angle_deg, int radius, bool is_large, bool is_filled) {
    float angle_rad = (angle_deg - 90) * 3.14159f / 180.0f;  // -90 to start from top
    int x = CIRCLE_CENTER_X + (int)(cos(angle_rad) * radius);
    int y = CIRCLE_CENTER_Y + (int)(sin(angle_rad) * radius);
    int dot_r = is_large ? DOT_RADIUS_LARGE : DOT_RADIUS_SMALL;

    if (is_filled) {
        canvas.fillCircle(x, y, dot_r, UI_WHITE);
    } else {
        canvas.drawCircle(x, y, dot_r, UI_WHITE);
    }
}

// Draw Layer 1: Menu Selection
void draw_menu_select() {
    canvas.fillScreen(UI_BLACK);

    // Draw circle outline
    canvas.drawCircle(CIRCLE_CENTER_X, CIRCLE_CENTER_Y, CIRCLE_RADIUS, UI_WHITE);

    // Draw menu dots (5 items evenly spaced)
    for (int i = 0; i < MODE_MAX; i++) {
        float angle = (360.0f / MODE_MAX) * i;
        bool is_selected = (i == current_mode);
        draw_circle_dot(angle, CIRCLE_RADIUS, is_selected, is_selected);
    }

    // Center: Current menu name
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::lgfxJapanGothicP_28);
    canvas.setTextColor(UI_WHITE);
    canvas.drawString(mode_names[current_mode], CIRCLE_CENTER_X, CIRCLE_CENTER_Y);

    // Bottom: "変更"
    canvas.setFont(&fonts::lgfxJapanGothicP_16);
    canvas.setTextColor(UI_GRAY);
    canvas.drawString("変更", CIRCLE_CENTER_X, 200);
}

// Arc parameters for Layer 2
#define ARC_START_ANGLE  135   // Left end (0%)
#define ARC_END_ANGLE    45    // Right end (100%)
#define ARC_SPAN         270   // Total arc span (going over the top)

// Draw Layer 2: Value Adjustment
void draw_value_adjust() {
    canvas.fillScreen(UI_BLACK);

    // Draw arc (open at bottom): from 135° to 45° going through top
    // LovyanGFX drawArc: x, y, r1, r2, angle1, angle2
    canvas.drawArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y, CIRCLE_RADIUS - 1, CIRCLE_RADIUS + 1,
                   ARC_START_ANGLE, 360, UI_WHITE);  // 135° to 360°
    canvas.drawArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y, CIRCLE_RADIUS - 1, CIRCLE_RADIUS + 1,
                   0, ARC_END_ANGLE, UI_WHITE);       // 0° to 45°

    // Calculate current value as percentage (0.0 - 1.0)
    float value_pct = 0.0f;
    char value_str[32];

    switch (current_mode) {
        case MODE_HUE:
            value_pct = led_hue / 360.0f;
            snprintf(value_str, sizeof(value_str), "%d°", led_hue);
            break;
        case MODE_SATURATION:
            value_pct = led_saturation / 255.0f;
            snprintf(value_str, sizeof(value_str), "%d%%", led_saturation * 100 / 255);
            break;
        case MODE_BRIGHTNESS:
            value_pct = led_brightness / 255.0f;
            snprintf(value_str, sizeof(value_str), "%d%%", led_brightness * 100 / 255);
            break;
        case MODE_COUNT:
            value_pct = (led_count - 1) / (float)(LED_STRIP_MAX_LEDS - 1);
            snprintf(value_str, sizeof(value_str), "%d", led_count);
            break;
        case MODE_EFFECT:
            value_pct = led_effect / (float)(NUM_EFFECTS - 1);
            snprintf(value_str, sizeof(value_str), "%s", effect_names[led_effect]);
            break;
        default:
            value_str[0] = '\0';
    }

    // Draw position dot on arc
    // Map value_pct (0-1) to angle: 0% = 135°, 100% = 45° (going through 270°/top)
    float angle_deg = ARC_START_ANGLE + value_pct * ARC_SPAN;  // 135 + pct * 270
    if (angle_deg >= 360) angle_deg -= 360;

    float angle_rad = angle_deg * 3.14159f / 180.0f;
    int dot_x = CIRCLE_CENTER_X + (int)(cos(angle_rad) * CIRCLE_RADIUS);
    int dot_y = CIRCLE_CENTER_Y + (int)(sin(angle_rad) * CIRCLE_RADIUS);
    canvas.fillCircle(dot_x, dot_y, DOT_RADIUS_LARGE, UI_WHITE);

    // Center: Menu name + value
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::lgfxJapanGothicP_20);
    canvas.setTextColor(UI_WHITE);
    canvas.drawString(mode_names[current_mode], CIRCLE_CENTER_X, CIRCLE_CENTER_Y - 15);

    canvas.setFont(&fonts::lgfxJapanGothicP_28);
    canvas.drawString(value_str, CIRCLE_CENTER_X, CIRCLE_CENTER_Y + 20);

    // Bottom: "決定"
    canvas.setFont(&fonts::lgfxJapanGothicP_16);
    canvas.setTextColor(UI_GRAY);
    canvas.drawString("決定", CIRCLE_CENTER_X, 200);
}

void update_display() {
    if (ota_in_progress) {
        canvas.fillScreen(UI_BLACK);
        canvas.setTextColor(UI_WHITE);
        canvas.setTextDatum(MC_DATUM);
        canvas.setFont(&fonts::lgfxJapanGothicP_20);
        canvas.drawString("更新中...", 120, 100);
        canvas.drawRoundRect(40, 130, 160, 12, 6, UI_WHITE);
        canvas.fillRoundRect(42, 132, (156 * ota_progress) / 100, 8, 4, UI_WHITE);
    } else if (in_adjustment_mode) {
        draw_value_adjust();
    } else {
        draw_menu_select();
    }

    canvas.pushSprite(0, 0);
}

// ===== WiFi and OTA =====

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", ip_address);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init() {
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    mdns_init();
    mdns_hostname_set("m5dial");
    mdns_instance_name_set("M5Dial LED Controller");
}

static esp_err_t ota_post_handler(httpd_req_t *req) {
    char buf[256];
    int received;
    int remaining = req->content_len;
    bool header_checked = false;

    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);

    if (update_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    ota_in_progress = true;
    ota_progress = 0;
    int total_size = remaining;

    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ota_in_progress = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (received <= 0) {
            esp_ota_abort(ota_handle);
            ota_in_progress = false;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        if (!header_checked) {
            if (received > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                esp_app_desc_t *app_desc = (esp_app_desc_t*)(buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
                ESP_LOGI(TAG, "New firmware version: %s", app_desc->version);
                header_checked = true;
            }
        }

        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            ota_in_progress = false;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= received;
        ota_progress = ((total_size - remaining) * 100) / total_size;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ota_in_progress = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ota_in_progress = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OTA Success! Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    const char *html = "<html><body><h1>M5Dial LED Controller OTA</h1>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='firmware'><input type='submit' value='Update'></form></body></html>";
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

void start_ota_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t ota_uri = {.uri = "/update", .method = HTTP_POST, .handler = ota_post_handler};
        httpd_register_uri_handler(server, &ota_uri);
    }
}

// ===== Main =====

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "M5Dial LED Controller Starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize display
    display.init();
    display.setRotation(0);
    display.setBrightness(128);
    canvas.createSprite(240, 240);

    // Initialize buzzer
    buzzer_init();

    // Initialize WiFi and OTA server
    wifi_init();
    start_ota_server();

    // Initialize LED strip
    led_strip_init();

    // Initialize encoder
    gpio_config_t encoder_conf = {
        .pin_bit_mask = (1ULL << ENCODER_A_PIN) | (1ULL << ENCODER_B_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&encoder_conf);

    gpio_config_t button_conf = {
        .pin_bit_mask = (1ULL << ENCODER_BTN_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&button_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)ENCODER_A_PIN, encoder_isr, NULL);
    gpio_isr_handler_add((gpio_num_t)ENCODER_B_PIN, encoder_isr, NULL);

    // Initial beep
    buzzer_beep(1000, 100);

    int32_t last_encoder = 0;

    // Main loop
    while (1) {
        update_button_state();

        if (ota_in_progress) {
            update_display();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Handle button press
        if (was_short_press()) {
            if (in_adjustment_mode) {
                // Layer 2 -> Layer 1: Confirm and go back
                in_adjustment_mode = false;
                buzzer_beep(1000, 30);
            } else {
                // Layer 1 -> Layer 2: Enter adjustment mode
                in_adjustment_mode = true;
                buzzer_beep(1500, 30);
            }
        }

        // Handle encoder rotation
        int32_t current_encoder = encoder_count;
        if (current_encoder != last_encoder) {
            int diff = current_encoder - last_encoder;
            last_encoder = current_encoder;

            if (in_adjustment_mode) {
                // Layer 2: Adjust value
                switch (current_mode) {
                    case MODE_HUE:
                        led_hue = (led_hue + diff * 10 + 360) % 360;
                        break;
                    case MODE_SATURATION:
                        led_saturation = (uint8_t)MAX(0, MIN(255, led_saturation + diff * 8));
                        break;
                    case MODE_BRIGHTNESS:
                        led_brightness = (uint8_t)MAX(0, MIN(255, led_brightness + diff * 8));
                        break;
                    case MODE_COUNT:
                        led_count = (uint8_t)MAX(1, MIN(LED_STRIP_MAX_LEDS, led_count + diff));
                        break;
                    case MODE_EFFECT:
                        led_effect = (led_effect + diff + NUM_EFFECTS) % NUM_EFFECTS;
                        break;
                    default:
                        break;
                }
            } else {
                // Layer 1: Change menu selection
                int new_mode = (current_mode + diff + MODE_MAX) % MODE_MAX;
                current_mode = (ControlMode)new_mode;
            }
        }

        // Update LEDs (always on for now, can add on/off later)
        update_leds();

        // Update display
        update_display();

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
