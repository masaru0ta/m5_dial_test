/**
 * M5Dial WS2812B LEDコントローラー
 *
 * 操作:
 * - エンコーダー回転: 値を調整 (モードに応じて色相/明るさ/LED数)
 * - 短押し: モード切替
 * - 長押し: LED オン/オフ切替
 *
 * モード:
 * 1. 色相 - 色を変更
 * 2. 彩度 - 彩度を変更
 * 3. 明るさ - 明るさを変更
 * 4. LED数 - 点灯するLEDの数
 * 5. エフェクト - アニメーション効果を選択
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

// ピン定義
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

// WS2812B設定
#define LED_STRIP_PIN GPIO_NUM_15  // Grove Port A - GPIO15 (白線 / SCL)
#define LED_STRIP_MAX_LEDS 150     // 最大LED数

// M5Dialディスプレイクラス
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

// グローバルインスタンス
LGFX_M5Dial display;
LGFX_Sprite canvas(&display);
led_strip_handle_t led_strip = NULL;

// LED状態
uint16_t led_hue = 0;         // 0-359
uint8_t led_saturation = 255; // 0-255
uint8_t led_brightness = 128; // 0-255
uint8_t led_count = 10;       // 点灯するLED数
uint8_t led_effect = 0;       // 現在のエフェクト
uint8_t effect_speed = 5;     // エフェクト速度 1-9 (1=遅い, 9=速い)
int16_t control_position = 0; // インタラクティブ制御位置 (0〜led_count-1, ラップ)
bool control_active = false;  // コントロールモードレイヤー2の時true
bool led_on = true;           // LED オン/オフ

// エフェクト名
const char* effect_names[] = {
    "単色",
    "追いかけ",
    "往復",
    "コメット",
    "レインボー",
    "ランダム",
    "蛍",
    "ランダム蛍",
    "心拍",
    "Xmas Song"
};
#define NUM_EFFECTS 10

// Xmas Songメロディデータ (ジングルベル)
// 注: 周波数はHz、0 = 休符
struct MelodyNote {
    uint16_t freq;    // 周波数 (Hz)
    uint8_t duration; // 長さ (1単位 = 基本テンポ)
    uint16_t hue;     // この音の色相
};

// 音階の周波数
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_REST 0

// ジングルベルのメロディと色 (正確な楽譜)
// ハ長調 - 色: C=赤, D=オレンジ, E=黄, F=緑, G=シアン
// 注: VerseとChorusは同じメロディパターン
const MelodyNote xmas_melody[] = {
    // === 1番 ===
    // 「走れそりよ」 (E E E-)
    {NOTE_E4, 1, 60},  {NOTE_E4, 1, 60},  {NOTE_E4, 2, 60},
    // 「風のように」 (E E E-)
    {NOTE_E4, 1, 60},  {NOTE_E4, 1, 60},  {NOTE_E4, 2, 60},
    // 「雪の中を」 (E G C D)
    {NOTE_E4, 1, 60},  {NOTE_G4, 1, 180}, {NOTE_C4, 1, 0},   {NOTE_D4, 1, 30},
    // 「軽く」 (E-)
    {NOTE_E4, 2, 60},  {NOTE_REST, 1, 0},
    // 「鈴が鳴る」 (F F F F)
    {NOTE_F4, 1, 120}, {NOTE_F4, 1, 120}, {NOTE_F4, 1, 120}, {NOTE_F4, 1, 120},
    // 「リンリンリン」 (F E E E)
    {NOTE_F4, 1, 120}, {NOTE_E4, 1, 60},  {NOTE_E4, 1, 60},  {NOTE_E4, 1, 60},
    // 「鈴が鳴る」 (E D D E)
    {NOTE_E4, 1, 60},  {NOTE_D4, 1, 30},  {NOTE_D4, 1, 30},  {NOTE_E4, 1, 60},
    // 「楽しいな」 (D- G-)
    {NOTE_D4, 2, 30},  {NOTE_G4, 2, 180},
    {NOTE_REST, 2, 0},
    // === サビ ===
    // 「ジングルベル」 (E E E-, E E E-)
    {NOTE_E4, 1, 60},  {NOTE_E4, 1, 60},  {NOTE_E4, 2, 60},
    {NOTE_E4, 1, 60},  {NOTE_E4, 1, 60},  {NOTE_E4, 2, 60},
    // 「ジングルベル」 (E G C D E-)
    {NOTE_E4, 1, 60},  {NOTE_G4, 1, 180}, {NOTE_C4, 1, 0},   {NOTE_D4, 1, 30}, {NOTE_E4, 2, 60},
    {NOTE_REST, 1, 0},
    // 「鈴が鳴る」 (F F F F F E E E)
    {NOTE_F4, 1, 120}, {NOTE_F4, 1, 120}, {NOTE_F4, 1, 120}, {NOTE_F4, 1, 120},
    {NOTE_F4, 1, 120}, {NOTE_E4, 1, 60},  {NOTE_E4, 1, 60},  {NOTE_E4, 1, 60},
    // 「楽しいそり遊び」 (G G F D C-)
    {NOTE_G4, 1, 180}, {NOTE_G4, 1, 180}, {NOTE_F4, 1, 120}, {NOTE_D4, 1, 30}, {NOTE_C4, 2, 0},
    {NOTE_REST, 4, 0},
};
#define XMAS_MELODY_LENGTH (sizeof(xmas_melody) / sizeof(xmas_melody[0]))

// コントロールモード
enum ControlMode {
    MODE_HUE = 0,
    MODE_BRIGHTNESS,
    MODE_COUNT,
    MODE_EFFECT,
    MODE_SPEED,
    MODE_CONTROL,
    MODE_MAX
};

// モード名
const char* mode_names[] = {
    "色相",
    "明るさ",
    "LED数",
    "エフェクト",
    "スピード",
    "コントロール"
};

ControlMode current_mode = MODE_HUE;

// エンコーダー状態
volatile int32_t encoder_count = 0;
static int8_t last_state = 0;

// ボタン状態
static bool last_button_state = false;
static uint32_t button_press_time = 0;
static bool button_was_long_press = false;
static bool button_just_released_short = false;
static const uint32_t LONG_PRESS_MS = 300;

// WiFi状態
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static char ip_address[16] = "";
static bool ota_in_progress = false;
static int ota_progress = 0;

// ===== HSVからRGBへの変換 =====

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

// ===== LEDストリップ関数 =====

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

    // 初期化時にLEDをクリア
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
    effect_counter += effect_speed;  // 速度がエフェクトのアニメーション速度を制御

    // Xmas Songエフェクト以外の時はブザーを停止
    static uint8_t last_effect = 255;
    if (led_effect != 9 && last_effect == 9) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    last_effect = led_effect;

    uint8_t r, g, b;

    // 蛍の状態 (呼び出し間で永続)
    static uint8_t firefly_brightness[LED_STRIP_MAX_LEDS] = {0};
    static int8_t firefly_direction[LED_STRIP_MAX_LEDS] = {0};
    static uint16_t firefly_hue[LED_STRIP_MAX_LEDS] = {0};  // ランダム蛍用

    switch (led_effect) {
        case 0: // 単色
            hsv_to_rgb(led_hue, led_saturation, led_brightness, &r, &g, &b);
            for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                if (i < led_count) {
                    if (control_active) {
                        // コントロールモード: control_positionの位置だけ点灯
                        if (i == control_position) {
                            led_strip_set_pixel(led_strip, i, r, g, b);
                        } else {
                            led_strip_set_pixel(led_strip, i, r/15, g/15, b/15);
                        }
                    } else {
                        led_strip_set_pixel(led_strip, i, r, g, b);
                    }
                } else {
                    led_strip_set_pixel(led_strip, i, 0, 0, 0);
                }
            }
            break;

        case 1: // 追いかけ (複数の光が流れる)
            {
                hsv_to_rgb(led_hue, led_saturation, led_brightness, &r, &g, &b);
                int num_chasers = 3;  // 追いかける光の数
                int spacing = led_count / num_chasers;
                int base_pos = control_active ? control_position : ((effect_counter / 2) % led_count);

                for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                    if (i < led_count) {
                        bool is_chaser = false;
                        for (int c = 0; c < num_chasers; c++) {
                            int chaser_pos = (base_pos + c * spacing) % led_count;
                            if (i == chaser_pos) {
                                is_chaser = true;
                                break;
                            }
                        }
                        if (is_chaser) {
                            led_strip_set_pixel(led_strip, i, r, g, b);
                        } else {
                            led_strip_set_pixel(led_strip, i, r/15, g/15, b/15);
                        }
                    } else {
                        led_strip_set_pixel(led_strip, i, 0, 0, 0);
                    }
                }
            }
            break;

        case 2: // 往復 (光が左右に跳ね返る)
            {
                hsv_to_rgb(led_hue, led_saturation, led_brightness, &r, &g, &b);
                int bounce_pos;
                if (control_active) {
                    bounce_pos = control_position;
                } else {
                    int cycle = (led_count - 1) * 2;
                    int pos_in_cycle = (effect_counter / 2) % cycle;
                    bounce_pos = (pos_in_cycle < led_count) ? pos_in_cycle : (cycle - pos_in_cycle);
                }

                for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                    if (i < led_count) {
                        if (i == bounce_pos) {
                            led_strip_set_pixel(led_strip, i, r, g, b);
                        } else {
                            led_strip_set_pixel(led_strip, i, r/15, g/15, b/15);
                        }
                    } else {
                        led_strip_set_pixel(led_strip, i, 0, 0, 0);
                    }
                }
            }
            break;

        case 3: // コメット (明るい頭部と減衰する尾)
            {
                hsv_to_rgb(led_hue, led_saturation, led_brightness, &r, &g, &b);
                int comet_head = control_active ? control_position : ((effect_counter / 2) % led_count);
                int tail_length = 5;

                for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                    if (i < led_count) {
                        int distance = (comet_head - i + led_count) % led_count;
                        if (distance == 0) {
                            // 頭部 - 最大輝度
                            led_strip_set_pixel(led_strip, i, r, g, b);
                        } else if (distance <= tail_length) {
                            // 尾 - 減衰
                            float fade = 1.0f - (float)distance / (tail_length + 1);
                            led_strip_set_pixel(led_strip, i, (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade));
                        } else {
                            led_strip_set_pixel(led_strip, i, 0, 0, 0);
                        }
                    } else {
                        led_strip_set_pixel(led_strip, i, 0, 0, 0);
                    }
                }
            }
            break;

        case 4: // レインボー (虹色が流れる)
            {
                int offset = control_active ? (control_position * 360 / led_count) : (effect_counter * 2);
                for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                    if (i < led_count) {
                        uint16_t hue = (i * 360 / led_count + offset) % 360;
                        hsv_to_rgb(hue, led_saturation, led_brightness, &r, &g, &b);
                        led_strip_set_pixel(led_strip, i, r, g, b);
                    } else {
                        led_strip_set_pixel(led_strip, i, 0, 0, 0);
                    }
                }
            }
            break;

        case 5: // ランダム点滅
            {
                // 速度が高いほど点滅が頻繁
                int blink_threshold = 20 - effect_speed;  // 速度1=19, 速度9=11
                for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                    if (i < led_count) {
                        if ((int)(esp_random() % blink_threshold) == 0) {
                            // ランダムな色と位置
                            uint16_t rand_hue = esp_random() % 360;
                            hsv_to_rgb(rand_hue, led_saturation, led_brightness, &r, &g, &b);
                            led_strip_set_pixel(led_strip, i, r, g, b);
                        } else {
                            led_strip_set_pixel(led_strip, i, 0, 0, 0);
                        }
                    } else {
                        led_strip_set_pixel(led_strip, i, 0, 0, 0);
                    }
                }
            }
            break;

        case 6: // 蛍 (ランダム位置でゆっくりフェードイン/アウト)
            {
                hsv_to_rgb(led_hue, led_saturation, led_brightness, &r, &g, &b);

                // 速度が高いほどフェードが速く、開始が頻繁
                int fade_step = effect_speed;  // 速度1=1, 速度9=9
                int start_threshold = 110 - effect_speed * 10;  // 速度1=100, 速度9=20

                for (int i = 0; i < led_count; i++) {
                    // ランダムに光り始める
                    if (firefly_brightness[i] == 0 && firefly_direction[i] == 0) {
                        if ((int)(esp_random() % start_threshold) == 0) {
                            firefly_direction[i] = 1;  // フェードイン開始
                        }
                    }

                    // 輝度更新
                    if (firefly_direction[i] == 1) {
                        firefly_brightness[i] = (uint8_t)MIN(250, firefly_brightness[i] + fade_step);
                        if (firefly_brightness[i] >= 250) {
                            firefly_direction[i] = -1;  // フェードアウト開始
                        }
                    } else if (firefly_direction[i] == -1) {
                        if (firefly_brightness[i] > fade_step) {
                            firefly_brightness[i] -= fade_step;
                        } else {
                            firefly_brightness[i] = 0;
                            firefly_direction[i] = 0;  // 完了
                        }
                    }

                    float scale = firefly_brightness[i] / 255.0f;
                    led_strip_set_pixel(led_strip, i, (uint8_t)(r * scale), (uint8_t)(g * scale), (uint8_t)(b * scale));
                }

                // 未使用LEDをクリア
                for (int i = led_count; i < LED_STRIP_MAX_LEDS; i++) {
                    led_strip_set_pixel(led_strip, i, 0, 0, 0);
                }
            }
            break;

        case 7: // ランダム蛍 (ランダムな色の蛍)
            {
                // 速度が高いほどフェードが速く、開始が頻繁
                int fade_step = effect_speed;
                int start_threshold = 110 - effect_speed * 10;

                for (int i = 0; i < led_count; i++) {
                    // ランダムに光り始める (ランダムな色で)
                    if (firefly_brightness[i] == 0 && firefly_direction[i] == 0) {
                        if ((int)(esp_random() % start_threshold) == 0) {
                            firefly_direction[i] = 1;
                            firefly_hue[i] = esp_random() % 360;  // ランダムな色
                        }
                    }

                    // 輝度更新
                    if (firefly_direction[i] == 1) {
                        firefly_brightness[i] = (uint8_t)MIN(250, firefly_brightness[i] + fade_step);
                        if (firefly_brightness[i] >= 250) {
                            firefly_direction[i] = -1;
                        }
                    } else if (firefly_direction[i] == -1) {
                        if (firefly_brightness[i] > fade_step) {
                            firefly_brightness[i] -= fade_step;
                        } else {
                            firefly_brightness[i] = 0;
                            firefly_direction[i] = 0;
                        }
                    }

                    // 各蛍に個別のランダム色相を使用
                    float scale = firefly_brightness[i] / 255.0f;
                    uint8_t bright = (uint8_t)(led_brightness * scale);
                    hsv_to_rgb(firefly_hue[i], led_saturation, bright, &r, &g, &b);
                    led_strip_set_pixel(led_strip, i, r, g, b);
                }

                // 未使用LEDをクリア
                for (int i = led_count; i < LED_STRIP_MAX_LEDS; i++) {
                    led_strip_set_pixel(led_strip, i, 0, 0, 0);
                }
            }
            break;

        case 8: // 心拍
            {
                // 心拍パターン: 素早い二重パルス、その後休止
                int cycle = 100;
                int pos = effect_counter % cycle;
                float brightness_scale;

                if (pos < 10) {
                    // 第1拍 上昇
                    brightness_scale = pos / 10.0f;
                } else if (pos < 20) {
                    // 第1拍 下降
                    brightness_scale = 1.0f - (pos - 10) / 10.0f;
                } else if (pos < 30) {
                    // 第2拍 上昇
                    brightness_scale = (pos - 20) / 10.0f;
                } else if (pos < 40) {
                    // 第2拍 下降
                    brightness_scale = 1.0f - (pos - 30) / 10.0f;
                } else {
                    // 休止
                    brightness_scale = 0.0f;
                }

                uint8_t bright = (uint8_t)(led_brightness * brightness_scale);
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

        case 9: // クリスマスソング
            {
                // メロディ再生の状態
                static int melody_index = 0;
                static int note_timer = 0;
                static int note_duration_ticks = 0;
                static int current_led_pos = 0;
                static int16_t led_hues[LED_STRIP_MAX_LEDS];  // -1 = 消灯, 0-359 = 色相
                static bool xmas_initialized = false;
                static int last_control_pos = -1;

                // LED色相配列を初期化
                if (!xmas_initialized) {
                    for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                        led_hues[i] = -1;  // 全て消灯
                    }
                    xmas_initialized = true;
                    melody_index = 0;
                    current_led_pos = 0;
                    note_timer = 0;
                    note_duration_ticks = 0;
                }

                // テンポ制御: 速度が高いほどテンポが速い
                int base_duration = 15 - effect_speed;  // 速度1=14, 速度9=6 ティック/単位

                // 音を鳴らしてLEDを点灯する関数
                auto play_note = [&](int note_idx, int led_pos) {
                    const MelodyNote* note = &xmas_melody[note_idx];

                    // LED配列に色を保存 (以前の色を維持)
                    if (note->freq > 0) {
                        led_hues[led_pos] = note->hue;
                    }

                    // ブザーで音を鳴らす
                    if (note->freq > 0) {
                        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, note->freq);
                        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 256);
                        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                    } else {
                        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                    }
                };

                if (control_active) {
                    // コントロールモード: ユーザーが手動でメロディを進める
                    if (control_position != last_control_pos) {
                        // ダイヤルが動いた - 次の音を再生
                        int diff = control_position - last_control_pos;
                        if (diff > led_count / 2) diff -= led_count;
                        if (diff < -led_count / 2) diff += led_count;

                        if (diff > 0) {
                            // 前進 - 音を再生
                            for (int j = 0; j < diff; j++) {
                                melody_index = (melody_index + 1) % XMAS_MELODY_LENGTH;
                                current_led_pos = (current_led_pos + 1) % led_count;
                                play_note(melody_index, current_led_pos);
                            }
                            note_timer = 0;  // スタッカート用にタイマーリセット
                        } else if (diff < 0) {
                            // 後退 - LEDをクリア
                            for (int j = 0; j < -diff; j++) {
                                led_hues[current_led_pos] = -1;
                                current_led_pos = (current_led_pos - 1 + led_count) % led_count;
                                melody_index = (melody_index - 1 + XMAS_MELODY_LENGTH) % XMAS_MELODY_LENGTH;
                            }
                            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                        }
                        last_control_pos = control_position;
                    }
                    // スタッカート - 短時間後にブザーを停止
                    note_timer++;
                    if (note_timer > 8) {
                        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                    }
                } else {
                    // 自動モード: メロディを自動再生
                    last_control_pos = control_position;  // コントロールモード移行時の同期用

                    // 次の音を再生する必要があるか確認
                    if (note_timer >= note_duration_ticks) {
                        melody_index = (melody_index + 1) % XMAS_MELODY_LENGTH;
                        current_led_pos = (current_led_pos + 1) % led_count;

                        // 全LEDを巡ったらリセット
                        if (current_led_pos == 0) {
                            for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                                led_hues[i] = -1;
                            }
                        }

                        play_note(melody_index, current_led_pos);
                        note_duration_ticks = xmas_melody[melody_index].duration * base_duration;
                        note_timer = 0;
                    }

                    note_timer++;

                    // スタッカート効果
                    if (note_timer >= note_duration_ticks - 2) {
                        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
                        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
                    }
                }

                // 保存された色で全ての点灯LEDを表示
                for (int i = 0; i < LED_STRIP_MAX_LEDS; i++) {
                    if (i < led_count && led_hues[i] >= 0) {
                        hsv_to_rgb(led_hues[i], led_saturation, led_brightness, &r, &g, &b);
                        led_strip_set_pixel(led_strip, i, r, g, b);
                    } else {
                        led_strip_set_pixel(led_strip, i, 0, 0, 0);
                    }
                }
            }
            break;
    }

    led_strip_refresh(led_strip);
}

// ===== エンコーダーISR =====
// デテント位置でのみカウント (1クリック = 1ステップ)
// 状態00への最終遷移で方向を判定

static void IRAM_ATTR encoder_isr(void *arg) {
    uint8_t a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
    uint8_t b = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
    int8_t state = (a << 1) | b;

    // デテント(00)に戻った時だけカウント
    // どの状態から来たかで方向を判定
    if (state == 0b00) {
        if (last_state == 0b10) {
            encoder_count++;   // 時計回り: 10 -> 00
        } else if (last_state == 0b01) {
            encoder_count--;   // 反時計回り: 01 -> 00
        }
    }

    last_state = state;
}

// ===== ボタン処理 =====

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

// ===== ブザー =====

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

// ===== ディスプレイ =====

// シンプルなUIカラー
#define UI_BLACK   0x0000
#define UI_WHITE   0xFFFF
#define UI_GRAY    0x8410

// UI状態: false = レイヤー1 (メニュー選択), true = レイヤー2 (値調整)
static bool in_adjustment_mode = false;

// 円パラメータ
#define CIRCLE_CENTER_X  120
#define CIRCLE_CENTER_Y  120
#define CIRCLE_RADIUS    90
#define DOT_RADIUS_SMALL 5
#define DOT_RADIUS_LARGE 8

// 円上の角度位置にドットを描画
void draw_circle_dot(float angle_deg, int radius, bool is_large, bool is_filled) {
    float angle_rad = (angle_deg - 90) * 3.14159f / 180.0f;  // -90 to start from top
    int x = CIRCLE_CENTER_X + (int)(cos(angle_rad) * radius);
    int y = CIRCLE_CENTER_Y + (int)(sin(angle_rad) * radius);
    int dot_r = is_large ? DOT_RADIUS_LARGE : DOT_RADIUS_SMALL;

    if (is_filled) {
        canvas.fillCircle(x, y, dot_r, UI_WHITE);
    } else {
        canvas.fillCircle(x, y, dot_r, UI_BLACK);
        canvas.drawCircle(x, y, dot_r, UI_WHITE);
    }
}

// レイヤー1を描画: メニュー選択
void draw_menu_select() {
    canvas.fillScreen(UI_BLACK);

    // 円の輪郭を描画 (太くクリーンな線)
    canvas.fillArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y, CIRCLE_RADIUS - 1, CIRCLE_RADIUS + 1, 0, 360, UI_WHITE);

    // メニュードットを描画 (等間隔で配置)
    for (int i = 0; i < MODE_MAX; i++) {
        float angle = (360.0f / MODE_MAX) * i;
        bool is_selected = (i == current_mode);
        draw_circle_dot(angle, CIRCLE_RADIUS, is_selected, is_selected);
    }

    // 中央: 現在のメニュー名
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::lgfxJapanGothicP_28);
    canvas.setTextColor(UI_WHITE);
    canvas.drawString(mode_names[current_mode], CIRCLE_CENTER_X, CIRCLE_CENTER_Y);
}

// レイヤー2用の円弧パラメータ
#define ARC_START_ANGLE  135   // 左端 (0%)
#define ARC_END_ANGLE    45    // 右端 (100%)
#define ARC_SPAN         270   // 円弧の総スパン (上を経由)

// カラーホイールパラメータ
#define COLOR_WHEEL_SEGMENTS  12
#define COLOR_WHEEL_INNER_R   70
#define COLOR_WHEEL_OUTER_R   120

// 色相範囲の色名
const char* get_color_name(uint16_t hue) {
    if (hue < 15 || hue >= 345) return "RED";
    if (hue < 45) return "ORANGE";
    if (hue < 75) return "YELLOW";
    if (hue < 105) return "LIME";
    if (hue < 135) return "GREEN";
    if (hue < 165) return "SPRING";
    if (hue < 195) return "CYAN";
    if (hue < 225) return "SKY";
    if (hue < 255) return "BLUE";
    if (hue < 285) return "PURPLE";
    if (hue < 315) return "MAGENTA";
    return "ROSE";
}

// 色相選択用カラーホイールを描画
void draw_hue_wheel() {
    canvas.fillScreen(UI_BLACK);

    // 選択されたセグメントのインデックスを検索
    int selected_segment = (led_hue * COLOR_WHEEL_SEGMENTS / 360) % COLOR_WHEEL_SEGMENTS;

    // セグメント間のギャップ (度数)
    const float gap_angle = 6.0f;
    const float segment_angle = 360.0f / COLOR_WHEEL_SEGMENTS;

    // ギャップ付きでカラーホイールセグメントを描画
    for (int i = 0; i < COLOR_WHEEL_SEGMENTS; i++) {
        int hue = (i * 360) / COLOR_WHEEL_SEGMENTS;
        float base_angle = (float)i * segment_angle - 90;  // -90 to start from top

        // ギャップを作るためにセグメントを縮小 (開始にgap/2を追加、終了からgap/2を減算)
        float start_angle = base_angle + gap_angle / 2.0f;
        float end_angle = base_angle + segment_angle - gap_angle / 2.0f;

        // このセグメントのHSVをRGBに変換
        uint8_t r, g, b;
        hsv_to_rgb(hue, 255, 255, &r, &g, &b);
        uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

        // 塗りつぶし円弧セグメントを描画
        canvas.fillArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y,
                       COLOR_WHEEL_INNER_R, COLOR_WHEEL_OUTER_R,
                       start_angle, end_angle, color);

        // 選択セグメントに白枠を描画
        if (i == selected_segment) {
            // 太い白の円弧枠を描画 (内側と外側)
            for (int t = -1; t <= 1; t++) {
                canvas.drawArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y,
                               COLOR_WHEEL_INNER_R + t, COLOR_WHEEL_INNER_R + t + 1,
                               start_angle, end_angle, UI_WHITE);
                canvas.drawArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y,
                               COLOR_WHEEL_OUTER_R + t - 1, COLOR_WHEEL_OUTER_R + t,
                               start_angle, end_angle, UI_WHITE);
            }
            // 側面の線を描画 (太く)
            float rad1 = start_angle * 3.14159f / 180.0f;
            float rad2 = end_angle * 3.14159f / 180.0f;
            for (int t = -1; t <= 1; t++) {
                float perp1 = rad1 + 3.14159f / 2.0f;
                float perp2 = rad2 + 3.14159f / 2.0f;
                int ox1 = (int)(cos(perp1) * t);
                int oy1 = (int)(sin(perp1) * t);
                int ox2 = (int)(cos(perp2) * t);
                int oy2 = (int)(sin(perp2) * t);
                canvas.drawLine(
                    CIRCLE_CENTER_X + (int)(cos(rad1) * COLOR_WHEEL_INNER_R) + ox1,
                    CIRCLE_CENTER_Y + (int)(sin(rad1) * COLOR_WHEEL_INNER_R) + oy1,
                    CIRCLE_CENTER_X + (int)(cos(rad1) * COLOR_WHEEL_OUTER_R) + ox1,
                    CIRCLE_CENTER_Y + (int)(sin(rad1) * COLOR_WHEEL_OUTER_R) + oy1, UI_WHITE);
                canvas.drawLine(
                    CIRCLE_CENTER_X + (int)(cos(rad2) * COLOR_WHEEL_INNER_R) + ox2,
                    CIRCLE_CENTER_Y + (int)(sin(rad2) * COLOR_WHEEL_INNER_R) + oy2,
                    CIRCLE_CENTER_X + (int)(cos(rad2) * COLOR_WHEEL_OUTER_R) + ox2,
                    CIRCLE_CENTER_Y + (int)(sin(rad2) * COLOR_WHEEL_OUTER_R) + oy2, UI_WHITE);
            }
        }
    }

    // 選択セグメントの色で中央円を描画 (ホイールと同じ色)
    int display_hue = (selected_segment * 360) / COLOR_WHEEL_SEGMENTS;
    uint8_t r, g, b;
    hsv_to_rgb(display_hue, 255, 255, &r, &g, &b);
    uint16_t center_color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

    int center_radius = COLOR_WHEEL_INNER_R - 15;
    canvas.fillCircle(CIRCLE_CENTER_X, CIRCLE_CENTER_Y, center_radius, center_color);

    // 中央に色名を描画 (明るい色でも見やすいよう黒文字)
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::Font4);
    canvas.setTextColor(UI_BLACK);
    canvas.drawString(get_color_name(display_hue), CIRCLE_CENTER_X, CIRCLE_CENTER_Y);
}

// エフェクト選択を描画 (レイヤー1と同じスタイル)
void draw_effect_select() {
    canvas.fillScreen(UI_BLACK);

    // 円の輪郭を描画 (太くクリーンな線)
    canvas.fillArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y, CIRCLE_RADIUS - 1, CIRCLE_RADIUS + 1, 0, 360, UI_WHITE);

    // エフェクトドットを描画 (等間隔で配置)
    for (int i = 0; i < NUM_EFFECTS; i++) {
        float angle = (360.0f / NUM_EFFECTS) * i;
        float angle_rad = (angle - 90) * 3.14159f / 180.0f;
        int x = CIRCLE_CENTER_X + (int)(cos(angle_rad) * CIRCLE_RADIUS);
        int y = CIRCLE_CENTER_Y + (int)(sin(angle_rad) * CIRCLE_RADIUS);
        int dot_r = (i == led_effect) ? DOT_RADIUS_LARGE : DOT_RADIUS_SMALL;

        if (i == led_effect) {
            canvas.fillCircle(x, y, dot_r, UI_WHITE);
        } else {
            canvas.fillCircle(x, y, dot_r, UI_BLACK);
            canvas.drawCircle(x, y, dot_r, UI_WHITE);
        }
    }

    // 中央: エフェクト名
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::lgfxJapanGothicP_28);
    canvas.setTextColor(UI_WHITE);
    canvas.drawString(effect_names[led_effect], CIRCLE_CENTER_X, CIRCLE_CENTER_Y);
}

// コントロールモードUIを描画
void draw_control_mode() {
    canvas.fillScreen(UI_BLACK);

    // 完全な円を描画
    canvas.fillArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y, CIRCLE_RADIUS - 1, CIRCLE_RADIUS + 1, 0, 360, UI_WHITE);

    // 円上に位置インジケーターを描画
    float angle_deg = (360.0f * control_position / led_count) - 90;  // -90 to start from top
    float angle_rad = angle_deg * 3.14159f / 180.0f;
    int dot_x = CIRCLE_CENTER_X + (int)(cos(angle_rad) * CIRCLE_RADIUS);
    int dot_y = CIRCLE_CENTER_Y + (int)(sin(angle_rad) * CIRCLE_RADIUS);
    canvas.fillCircle(dot_x, dot_y, DOT_RADIUS_LARGE, UI_WHITE);

    // 中央: エフェクト名と位置
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::lgfxJapanGothicP_20);
    canvas.setTextColor(UI_WHITE);
    canvas.drawString(effect_names[led_effect], CIRCLE_CENTER_X, CIRCLE_CENTER_Y - 20);

    char pos_str[16];
    snprintf(pos_str, sizeof(pos_str), "%d / %d", control_position + 1, led_count);
    canvas.setFont(&fonts::lgfxJapanGothicP_28);
    canvas.drawString(pos_str, CIRCLE_CENTER_X, CIRCLE_CENTER_Y + 20);
}

// レイヤー2を描画: 値調整
void draw_value_adjust() {
    // 色相選択用の特別UI
    if (current_mode == MODE_HUE) {
        draw_hue_wheel();
        return;
    }

    // エフェクト選択用の特別UI (レイヤー1スタイルと同じ)
    if (current_mode == MODE_EFFECT) {
        draw_effect_select();
        return;
    }

    // コントロールモード用の特別UI
    if (current_mode == MODE_CONTROL) {
        draw_control_mode();
        return;
    }

    canvas.fillScreen(UI_BLACK);

    // 円弧を描画 (下部が開いている): 135°から45°まで上部を経由
    canvas.drawArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y, CIRCLE_RADIUS - 1, CIRCLE_RADIUS + 1,
                   ARC_START_ANGLE, 360, UI_WHITE);
    canvas.drawArc(CIRCLE_CENTER_X, CIRCLE_CENTER_Y, CIRCLE_RADIUS - 1, CIRCLE_RADIUS + 1,
                   0, ARC_END_ANGLE, UI_WHITE);

    // 現在値をパーセンテージで計算 (0.0 - 1.0)
    float value_pct = 0.0f;
    char value_str[32];

    switch (current_mode) {
        case MODE_BRIGHTNESS:
            value_pct = led_brightness / 255.0f;
            snprintf(value_str, sizeof(value_str), "%d%%", led_brightness * 100 / 255);
            break;
        case MODE_COUNT:
            value_pct = (led_count - 1) / (float)(LED_STRIP_MAX_LEDS - 1);
            snprintf(value_str, sizeof(value_str), "%d", led_count);
            break;
        case MODE_SPEED:
            value_pct = (effect_speed - 1) / 8.0f;  // 1-9 mapped to 0-1
            snprintf(value_str, sizeof(value_str), "%d", effect_speed);
            break;
        default:
            value_str[0] = '\0';
    }

    // 円弧上に位置ドットを描画
    float angle_deg = ARC_START_ANGLE + value_pct * ARC_SPAN;
    if (angle_deg >= 360) angle_deg -= 360;

    float angle_rad = angle_deg * 3.14159f / 180.0f;
    int dot_x = CIRCLE_CENTER_X + (int)(cos(angle_rad) * CIRCLE_RADIUS);
    int dot_y = CIRCLE_CENTER_Y + (int)(sin(angle_rad) * CIRCLE_RADIUS);
    canvas.fillCircle(dot_x, dot_y, DOT_RADIUS_LARGE, UI_WHITE);

    // 中央: メニュー名 + 値
    canvas.setTextDatum(MC_DATUM);
    canvas.setFont(&fonts::lgfxJapanGothicP_20);
    canvas.setTextColor(UI_WHITE);
    canvas.drawString(mode_names[current_mode], CIRCLE_CENTER_X, CIRCLE_CENTER_Y - 15);

    canvas.setFont(&fonts::lgfxJapanGothicP_28);
    canvas.drawString(value_str, CIRCLE_CENTER_X, CIRCLE_CENTER_Y + 20);
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

// ===== WiFiとOTA =====

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

// ===== メイン =====

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "M5Dial LEDコントローラー開始...");

    // NVS初期化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ディスプレイ初期化
    display.init();
    display.setRotation(0);
    display.setBrightness(128);
    canvas.createSprite(240, 240);

    // ブザー初期化
    buzzer_init();

    // WiFiとOTAサーバー初期化
    wifi_init();
    start_ota_server();

    // LEDストリップ初期化
    led_strip_init();

    // エンコーダー初期化
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

    // 起動ビープ
    buzzer_beep(1000, 100);

    int32_t last_encoder = 0;

    // メインループ
    while (1) {
        update_button_state();

        if (ota_in_progress) {
            update_display();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // ボタン押下処理
        if (was_short_press()) {
            if (in_adjustment_mode) {
                // レイヤー2 -> レイヤー1: 確定して戻る
                in_adjustment_mode = false;
                control_active = false;
                buzzer_beep(1000, 30);
            } else {
                // レイヤー1 -> レイヤー2: 調整モードに入る
                in_adjustment_mode = true;
                if (current_mode == MODE_CONTROL) {
                    control_active = true;
                }
                buzzer_beep(1500, 30);
            }
        }

        // エンコーダー回転処理
        int32_t current_encoder = encoder_count;
        if (current_encoder != last_encoder) {
            int diff = current_encoder - last_encoder;
            last_encoder = current_encoder;

            if (in_adjustment_mode) {
                // レイヤー2: 値を調整
                switch (current_mode) {
                    case MODE_HUE:
                        // 12セグメント = 1ステップあたり30度 (360/12)
                        led_hue = (led_hue + diff * 30 + 360) % 360;
                        break;
                    case MODE_BRIGHTNESS:
                        // 20%ステップ (255 / 5 = 51)
                        led_brightness = (uint8_t)MAX(0, MIN(255, led_brightness + diff * 51));
                        break;
                    case MODE_COUNT:
                        led_count = (uint8_t)MAX(1, MIN(LED_STRIP_MAX_LEDS, led_count + diff));
                        break;
                    case MODE_EFFECT:
                        led_effect = (led_effect + diff + NUM_EFFECTS) % NUM_EFFECTS;
                        break;
                    case MODE_SPEED:
                        effect_speed = (uint8_t)MAX(1, MIN(9, effect_speed + diff));
                        break;
                    case MODE_CONTROL:
                        // led_countに基づいてラップアラウンド
                        control_position = (control_position + diff + led_count) % led_count;
                        break;
                    default:
                        break;
                }
            } else {
                // レイヤー1: メニュー選択を変更
                int new_mode = (current_mode + diff + MODE_MAX) % MODE_MAX;
                current_mode = (ControlMode)new_mode;
            }
        }

        // LED更新 (現在は常時オン、後でオン/オフ追加可能)
        update_leds();

        // ディスプレイ更新
        update_display();

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
