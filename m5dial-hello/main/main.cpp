/**
 * M5Dial Hello Worldサンプル
 *
 * 機能:
 * - LCDに「Hello World」を表示
 * - エンコーダー回転でカウンター変更
 * - エンコーダーボタン押下でカウンターリセット
 */

#include <stdio.h>
#include <string.h>
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

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "wifi_credentials.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "M5Dial-Hello";

// LCDピン定義
#define LCD_MOSI_PIN 5
#define LCD_SCLK_PIN 6
#define LCD_DC_PIN   4
#define LCD_CS_PIN   7
#define LCD_RST_PIN  8
#define LCD_BL_PIN   9

// エンコーダーピン定義
#define ENCODER_A_PIN 41
#define ENCODER_B_PIN 40
#define ENCODER_BTN_PIN 42

// ブザーピン定義
#define BUZZER_PIN 3

// M5Dialディスプレイクラス
class LGFX_M5Dial : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX_M5Dial(void) {
        // SPIバス設定
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

        // パネル設定
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

        // バックライト設定
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
LGFX_Sprite canvas(&display);  // オフスクリーンバッファ
int32_t counter = 0;
int32_t last_encoder_value = 0;

// WiFi状態
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static char ip_address[16] = "Connecting...";
static bool ota_in_progress = false;
static int ota_progress = 0;

// エンコーダー割り込み変数
volatile int32_t encoder_count = 0;
volatile int32_t encoder_raw = 0;
static int8_t last_state = 0;

// 直交エンコーダー状態テーブル: [前回状態][現在状態] -> 方向 (-1, 0, +1)
// 状態: 00=0, 01=1, 11=2, 10=3 (グレイコード順)
static const int8_t quad_table[4][4] = {
    // 行先:  0   1   2   3   元:
    {  0, +1,  0, -1 },  // 0
    { -1,  0, +1,  0 },  // 1
    {  0, -1,  0, +1 },  // 2
    { +1,  0, -1,  0 },  // 3
};

// エンコーダーISR - 直交デコード
static void IRAM_ATTR encoder_isr(void* arg) {
    int a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
    int b = gpio_get_level((gpio_num_t)ENCODER_B_PIN);

    // グレイコード状態に変換 (0,1,3,2 -> 0,1,2,3)
    int8_t state = (a << 1) | (a ^ b);

    // 状態遷移から方向を取得
    int8_t dir = quad_table[last_state][state];
    encoder_raw += dir;
    last_state = state;

    // 生カウントをデテントカウントに変換 (1デテント = 4パルス)
    encoder_count = encoder_raw / 4;
}

// ブザーリクエストフラグ (ISRで設定、メインループで処理)
volatile bool buzzer_click_request = false;
volatile bool buzzer_reset_request = false;

// ボタンISR
static void IRAM_ATTR button_isr(void* arg) {
    counter = 0;
    encoder_count = 0;
    encoder_raw = 0;
    buzzer_reset_request = true;
}

// ブザー初期化
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

    ESP_LOGI(TAG, "ブザー初期化完了");
}

// 短いビープ音を鳴らす
void buzzer_beep(uint32_t freq, uint32_t duration_ms) {
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);  // 50%デューティ
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// エンコーダー初期化
void encoder_init() {
    // エンコーダーピンを入力・プルアップ・両エッジ検出で設定
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ENCODER_A_PIN) | (1ULL << ENCODER_B_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // ボタンピンを設定
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << ENCODER_BTN_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    // 初期状態を取得
    int a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
    int b = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
    last_state = (a << 1) | (a ^ b);

    // GPIO ISRサービスをインストール
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)ENCODER_A_PIN, encoder_isr, NULL);
    gpio_isr_handler_add((gpio_num_t)ENCODER_B_PIN, encoder_isr, NULL);
    gpio_isr_handler_add((gpio_num_t)ENCODER_BTN_PIN, button_isr, NULL);

    ESP_LOGI(TAG, "エンコーダー初期化完了");
}

// ディスプレイ更新 (スプライトでちらつき防止)
void update_display() {
    canvas.fillScreen(TFT_BLACK);

    if (ota_in_progress) {
        // OTA進捗画面
        canvas.setTextColor(TFT_YELLOW);
        canvas.setTextDatum(MC_DATUM);
        canvas.setFont(&fonts::FreeSansBold18pt7b);
        canvas.drawString("Updating...", 120, 80);

        // プログレスバー
        canvas.drawRect(30, 110, 180, 20, TFT_WHITE);
        canvas.fillRect(32, 112, (176 * ota_progress) / 100, 16, TFT_GREEN);

        canvas.setFont(&fonts::FreeSans12pt7b);
        canvas.setTextColor(TFT_WHITE);
        char progress_str[8];
        snprintf(progress_str, sizeof(progress_str), "%d%%", ota_progress);
        canvas.drawString(progress_str, 120, 160);
    } else {
        // 通常画面
        // タイトル描画
        canvas.setTextColor(TFT_WHITE);
        canvas.setTextDatum(MC_DATUM);
        canvas.setFont(&fonts::FreeSansBold18pt7b);
        canvas.drawString("Hello World", 120, 50);

        // カウンターラベル描画
        canvas.setFont(&fonts::FreeSans12pt7b);
        canvas.drawString("Counter:", 120, 100);

        // カウンター値描画
        canvas.setFont(&fonts::FreeSansBold24pt7b);
        canvas.setTextColor(TFT_CYAN);
        canvas.drawNumber(counter, 120, 140);

        // WiFi状態描画
        canvas.setFont(&fonts::Font0);
        canvas.setTextColor(TFT_GREEN);
        canvas.drawString(ip_address, 120, 190);

        // 操作説明描画
        canvas.setTextColor(TFT_LIGHTGREY);
        canvas.drawString("Rotate: Change | Press: Reset", 120, 220);
    }

    // 一括でディスプレイに転送
    canvas.pushSprite(0, 0);
}

// ===== WiFi関数 =====
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        strcpy(ip_address, "Reconnecting..");
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
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialized, connecting to %s", WIFI_SSID);
}

// ===== mDNS関数 =====
void mdns_init_service() {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("m5dial"));
    ESP_ERROR_CHECK(mdns_instance_name_set("M5Dial OTA Server"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
    ESP_LOGI(TAG, "mDNS initialized: m5dial.local");
}

// ===== OTA HTTPサーバー =====
static esp_ota_handle_t ota_handle = 0;
static const esp_partition_t *ota_partition = NULL;

static esp_err_t ota_post_handler(httpd_req_t *req) {
    char buf[256];
    int received;
    int remaining = req->content_len;
    bool is_first_chunk = true;

    ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA started, size: %d, partition: %s", remaining, ota_partition->label);
    ota_in_progress = true;
    ota_progress = 0;
    update_display();

    int total_size = remaining;

    while (remaining > 0) {
        received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            ota_in_progress = false;
            return ESP_FAIL;
        }

        if (is_first_chunk) {
            is_first_chunk = false;
            if (esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
                ota_in_progress = false;
                return ESP_FAIL;
            }
        }

        if (esp_ota_write(ota_handle, buf, received) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            esp_ota_abort(ota_handle);
            ota_in_progress = false;
            return ESP_FAIL;
        }

        remaining -= received;
        ota_progress = ((total_size - remaining) * 100) / total_size;
        update_display();
    }

    if (esp_ota_end(ota_handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        ota_in_progress = false;
        return ESP_FAIL;
    }

    if (esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        ota_in_progress = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA successful, restarting...");
    httpd_resp_sendstr(req, "OTA Success! Rebooting...");

    buzzer_beep(2000, 200);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;
}

static const char *html_page =
    "<!DOCTYPE html><html><head><title>M5Dial OTA</title>"
    "<style>body{font-family:Arial;text-align:center;padding:50px;}"
    "h1{color:#333;}form{margin:20px;}"
    "input[type=file]{margin:10px;}input[type=submit]{padding:10px 20px;}</style></head>"
    "<body><h1>M5Dial OTA Update</h1>"
    "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin'><br>"
    "<input type='submit' value='Update Firmware'></form></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, html_page, strlen(html_page));
    return ESP_OK;
}

void start_ota_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t ota_uri = {
            .uri = "/update",
            .method = HTTP_POST,
            .handler = ota_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ota_uri);

        ESP_LOGI(TAG, "OTA server started on port 80");
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "M5Dial Hello World 開始...");

    // NVS初期化 (WiFiに必要)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ディスプレイ初期化
    display.init();
    display.setBrightness(128);
    display.setRotation(0);

    // スプライトバッファ作成 (240x240, 16ビットカラー)
    canvas.createSprite(240, 240);

    ESP_LOGI(TAG, "ディスプレイ初期化完了");

    // ブザー初期化
    buzzer_init();

    // エンコーダー初期化
    encoder_init();

    // 初期表示
    update_display();

    // WiFi初期化
    wifi_init();

    // WiFi接続待機 (タイムアウト付きで画面更新)
    for (int i = 0; i < 100; i++) {  // 10秒タイムアウト
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                               pdFALSE, pdFALSE, pdMS_TO_TICKS(100));
        if (bits & WIFI_CONNECTED_BIT) break;
        update_display();
    }

    // mDNSとOTAサーバー初期化
    mdns_init_service();
    start_ota_server();

    update_display();

    // 起動音
    buzzer_beep(2000, 50);

    ESP_LOGI(TAG, "メインループ開始");

    // メインループ
    while (1) {
        // ブザーリクエスト確認
        if (buzzer_reset_request) {
            buzzer_reset_request = false;
            buzzer_beep(1000, 100);  // リセット用の低音
            update_display();
        }

        // エンコーダー値変化確認
        int32_t current_encoder = encoder_count;
        if (current_encoder != last_encoder_value) {
            counter = current_encoder;
            last_encoder_value = current_encoder;
            update_display();
            buzzer_beep(4000, 10);  // 短いクリック音
            ESP_LOGI(TAG, "カウンター: %ld", counter);
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
