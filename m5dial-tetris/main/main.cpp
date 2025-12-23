/**
 * M5Dial Tetris Game
 *
 * Controls:
 * - Rotate encoder: Move piece left/right
 * - Press encoder: Rotate piece
 * - Hold encoder: Drop piece faster
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
#include "esp_random.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "wifi_credentials.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "M5Dial-Tetris";

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

// Tetris Constants
#define BOARD_WIDTH 10
#define BOARD_HEIGHT 20
#define BLOCK_SIZE 10
#define BOARD_X ((240 - BOARD_WIDTH * BLOCK_SIZE) / 2)
#define BOARD_Y 15

// Tetromino shapes (4 rotations each)
static const uint16_t TETROMINOES[7][4] = {
    // I
    {0x0F00, 0x2222, 0x00F0, 0x4444},
    // O
    {0xCC00, 0xCC00, 0xCC00, 0xCC00},
    // T
    {0x0E40, 0x4C40, 0x4E00, 0x4640},
    // S
    {0x06C0, 0x8C40, 0x6C00, 0x4620},
    // Z
    {0x0C60, 0x4C80, 0xC600, 0x2640},
    // J
    {0x0E80, 0xC440, 0x2E00, 0x44C0},
    // L
    {0x0E20, 0x44C0, 0x8E00, 0xC440},
};

// Colors for each tetromino
static const uint16_t TETRO_COLORS[7] = {
    0x07FF,  // I - Cyan
    0xFFE0,  // O - Yellow
    0xF81F,  // T - Purple
    0x07E0,  // S - Green
    0xF800,  // Z - Red
    0x001F,  // J - Blue
    0xFD20,  // L - Orange
};

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

// Game state
uint8_t board[BOARD_HEIGHT][BOARD_WIDTH] = {0};
int current_piece = 0;
int current_rotation = 0;
int piece_x = 3;
int piece_y = 0;
int next_piece = 0;
uint32_t score = 0;
uint32_t lines = 0;
uint32_t level = 1;
bool game_over = false;
bool game_paused = false;

// Encoder state
volatile int32_t encoder_count = 0;
volatile int32_t encoder_raw = 0;
// Button state now polled in main loop
static bool last_button_state = false;
static uint32_t button_press_time = 0;
static bool button_was_long_press = false;
static bool button_just_released_short = false;
static const uint32_t LONG_PRESS_MS = 150;
static int8_t last_state = 0;

// WiFi state
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static char ip_address[16] = "";
static bool ota_in_progress = false;
static int ota_progress = 0;

// State table for quadrature decoding
static const int8_t quad_table[4][4] = {
    {  0, +1,  0, -1 },
    { -1,  0, +1,  0 },
    {  0, -1,  0, +1 },
    { +1,  0, -1,  0 },
};

// Encoder ISR
static void IRAM_ATTR encoder_isr(void* arg) {
    int a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
    int b = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
    int8_t state = (a << 1) | (a ^ b);
    int8_t dir = quad_table[last_state][state];
    encoder_raw += dir;
    last_state = state;
    encoder_count = encoder_raw / 4;
}


// Buzzer functions
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
        .hpoint = 0,
        .flags = {0}
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

void play_move_sound() {
    buzzer_beep(800, 5);
}

void play_rotate_sound() {
    buzzer_beep(1200, 10);
}

void play_drop_sound() {
    buzzer_beep(400, 30);
}

void play_line_clear_sound() {
    buzzer_beep(1000, 50);
    vTaskDelay(pdMS_TO_TICKS(30));
    buzzer_beep(1200, 50);
    vTaskDelay(pdMS_TO_TICKS(30));
    buzzer_beep(1500, 100);
}

void play_game_over_sound() {
    for (int i = 0; i < 3; i++) {
        buzzer_beep(300, 200);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Encoder init
void encoder_init() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ENCODER_A_PIN) | (1ULL << ENCODER_B_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << ENCODER_BTN_PIN);
    gpio_config(&io_conf);

    int a = gpio_get_level((gpio_num_t)ENCODER_A_PIN);
    int b = gpio_get_level((gpio_num_t)ENCODER_B_PIN);
    last_state = (a << 1) | (a ^ b);

    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)ENCODER_A_PIN, encoder_isr, NULL);
    gpio_isr_handler_add((gpio_num_t)ENCODER_B_PIN, encoder_isr, NULL);
    // Button is polled, not using ISR
}

// ===== Tetris Game Logic =====

bool get_tetromino_cell(int piece, int rotation, int x, int y) {
    uint16_t shape = TETROMINOES[piece][rotation];
    int bit = y * 4 + x;
    return (shape >> (15 - bit)) & 1;
}

bool check_collision(int piece, int rotation, int px, int py) {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (get_tetromino_cell(piece, rotation, x, y)) {
                int bx = px + x;
                int by = py + y;
                if (bx < 0 || bx >= BOARD_WIDTH || by >= BOARD_HEIGHT) {
                    return true;
                }
                if (by >= 0 && board[by][bx] != 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

void lock_piece() {
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (get_tetromino_cell(current_piece, current_rotation, x, y)) {
                int bx = piece_x + x;
                int by = piece_y + y;
                if (by >= 0 && by < BOARD_HEIGHT && bx >= 0 && bx < BOARD_WIDTH) {
                    board[by][bx] = current_piece + 1;
                }
            }
        }
    }
}

int clear_lines() {
    int cleared = 0;
    for (int y = BOARD_HEIGHT - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (board[y][x] == 0) {
                full = false;
                break;
            }
        }
        if (full) {
            cleared++;
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < BOARD_WIDTH; x++) {
                    board[yy][x] = board[yy - 1][x];
                }
            }
            for (int x = 0; x < BOARD_WIDTH; x++) {
                board[0][x] = 0;
            }
            y++;  // Check same row again
        }
    }
    return cleared;
}

void spawn_piece() {
    current_piece = next_piece;
    next_piece = esp_random() % 7;
    current_rotation = 0;
    piece_x = 3;
    piece_y = 0;

    if (check_collision(current_piece, current_rotation, piece_x, piece_y)) {
        game_over = true;
    }
}

void new_game() {
    memset(board, 0, sizeof(board));
    score = 0;
    lines = 0;
    level = 1;
    game_over = false;
    next_piece = esp_random() % 7;
    spawn_piece();
    encoder_count = 0;
    encoder_raw = 0;
}

// ===== Drawing Functions =====

void draw_block(int x, int y, uint16_t color) {
    int px = BOARD_X + x * BLOCK_SIZE;
    int py = BOARD_Y + y * BLOCK_SIZE;
    canvas.fillRect(px + 1, py + 1, BLOCK_SIZE - 2, BLOCK_SIZE - 2, color);
    canvas.drawRect(px, py, BLOCK_SIZE, BLOCK_SIZE, 0x4208);
}

void draw_board() {
    // Draw border
    canvas.drawRect(BOARD_X - 1, BOARD_Y - 1,
                    BOARD_WIDTH * BLOCK_SIZE + 2,
                    BOARD_HEIGHT * BLOCK_SIZE + 2, TFT_WHITE);

    // Draw placed blocks
    for (int y = 0; y < BOARD_HEIGHT; y++) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            if (board[y][x] != 0) {
                draw_block(x, y, TETRO_COLORS[board[y][x] - 1]);
            }
        }
    }

    // Draw current piece
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (get_tetromino_cell(current_piece, current_rotation, x, y)) {
                int bx = piece_x + x;
                int by = piece_y + y;
                if (by >= 0) {
                    draw_block(bx, by, TETRO_COLORS[current_piece]);
                }
            }
        }
    }

    // Draw ghost piece
    int ghost_y = piece_y;
    while (!check_collision(current_piece, current_rotation, piece_x, ghost_y + 1)) {
        ghost_y++;
    }
    if (ghost_y != piece_y) {
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (get_tetromino_cell(current_piece, current_rotation, x, y)) {
                    int bx = piece_x + x;
                    int by = ghost_y + y;
                    if (by >= 0) {
                        int px = BOARD_X + bx * BLOCK_SIZE;
                        int py = BOARD_Y + by * BLOCK_SIZE;
                        canvas.drawRect(px + 2, py + 2, BLOCK_SIZE - 4, BLOCK_SIZE - 4,
                                        TETRO_COLORS[current_piece] & 0x7BEF);
                    }
                }
            }
        }
    }
}

void draw_next_piece() {
    // Position adjusted for circular display - move down from corner
    int nx = BOARD_X + BOARD_WIDTH * BLOCK_SIZE + 10;
    int ny = BOARD_Y + 40;

    canvas.setTextColor(TFT_WHITE);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString("NEXT", nx, ny - 12);

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (get_tetromino_cell(next_piece, 0, x, y)) {
                int px = nx + x * 6;
                int py = ny + y * 6;
                canvas.fillRect(px, py, 5, 5, TETRO_COLORS[next_piece]);
            }
        }
    }
}

void draw_score() {
    canvas.setTextColor(TFT_WHITE);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(TL_DATUM);

    // Position adjusted for circular display - move right and down
    int sx = 18;
    int sy = 55;

    canvas.drawString("SCORE", sx, sy);
    canvas.drawNumber(score, sx, sy + 12);

    canvas.drawString("LINES", sx, sy + 30);
    canvas.drawNumber(lines, sx, sy + 42);

    canvas.drawString("LEVEL", sx, sy + 60);
    canvas.drawNumber(level, sx, sy + 72);
}

void update_display() {
    canvas.fillScreen(TFT_BLACK);

    if (ota_in_progress) {
        canvas.setTextColor(TFT_YELLOW);
        canvas.setTextDatum(MC_DATUM);
        canvas.setFont(&fonts::FreeSansBold18pt7b);
        canvas.drawString("Updating...", 120, 100);
        canvas.drawRect(30, 120, 180, 20, TFT_WHITE);
        canvas.fillRect(32, 122, (176 * ota_progress) / 100, 16, TFT_GREEN);
    } else if (game_over) {
        canvas.setTextDatum(MC_DATUM);
        canvas.setFont(&fonts::FreeSansBold18pt7b);
        canvas.setTextColor(TFT_RED);
        canvas.drawString("GAME OVER", 120, 80);

        canvas.setFont(&fonts::FreeSans12pt7b);
        canvas.setTextColor(TFT_WHITE);
        canvas.drawString("Score:", 120, 130);
        canvas.drawNumber(score, 120, 160);

        canvas.setFont(&fonts::Font0);
        canvas.drawString("Press to restart", 120, 210);
    } else {
        draw_board();
        draw_next_piece();
        draw_score();
    }

    canvas.pushSprite(0, 0);
}

// ===== WiFi and OTA Functions =====

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
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void mdns_init_service() {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("m5dial"));
    ESP_ERROR_CHECK(mdns_instance_name_set("M5Dial Tetris"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
}

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

    ota_in_progress = true;
    ota_progress = 0;
    int total_size = remaining;

    while (remaining > 0) {
        received = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ota_in_progress = false;
            return ESP_FAIL;
        }

        if (is_first_chunk) {
            is_first_chunk = false;
            if (esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
                ota_in_progress = false;
                return ESP_FAIL;
            }
        }

        if (esp_ota_write(ota_handle, buf, received) != ESP_OK) {
            esp_ota_abort(ota_handle);
            ota_in_progress = false;
            return ESP_FAIL;
        }

        remaining -= received;
        ota_progress = ((total_size - remaining) * 100) / total_size;
    }

    if (esp_ota_end(ota_handle) != ESP_OK) {
        ota_in_progress = false;
        return ESP_FAIL;
    }

    if (esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        ota_in_progress = false;
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "OTA Success! Rebooting...");
    buzzer_beep(2000, 200);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    const char *html = "<html><body><h1>M5Dial Tetris OTA</h1>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='firmware'><input type='submit' value='Update'></form></body></html>";
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// ===== Button Handling =====

void update_button_state() {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    bool current_button = (gpio_get_level((gpio_num_t)ENCODER_BTN_PIN) == 0);
    button_just_released_short = false;

    if (current_button && !last_button_state) {
        // Button just pressed
        button_press_time = now;
        button_was_long_press = false;
    } else if (current_button && last_button_state) {
        // Button still held
        if (!button_was_long_press && (now - button_press_time) >= LONG_PRESS_MS) {
            button_was_long_press = true;
        }
    } else if (!current_button && last_button_state) {
        // Button just released
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
    ESP_LOGI(TAG, "M5Dial Tetris Starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Display
    display.init();
    display.setBrightness(128);
    display.setRotation(0);
    canvas.createSprite(240, 240);

    // Initialize peripherals
    buzzer_init();
    encoder_init();

    // Initialize WiFi and OTA
    wifi_init();
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));
    mdns_init_service();
    start_ota_server();

    // Start game
    new_game();
    buzzer_beep(1000, 100);

    int32_t last_encoder = 0;
    uint32_t last_drop = 0;
    uint32_t drop_interval = 1000;

    // Main loop
    while (1) {
        update_button_state();
        if (ota_in_progress) {
            update_display();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (game_over) {
            if (was_short_press()) {
                new_game();
                buzzer_beep(1000, 100);
            }
            update_display();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Handle rotation (button press)
        if (was_short_press()) {
            int new_rotation = (current_rotation + 1) % 4;
            if (!check_collision(current_piece, new_rotation, piece_x, piece_y)) {
                current_rotation = new_rotation;
                play_rotate_sound();
            }
        }

        // Handle horizontal movement (encoder)
        int32_t current_encoder = encoder_count;
        if (current_encoder != last_encoder) {
            int diff = current_encoder - last_encoder;
            int new_x = piece_x + diff;
            if (!check_collision(current_piece, current_rotation, new_x, piece_y)) {
                piece_x = new_x;
                play_move_sound();
            }
            last_encoder = current_encoder;
        }

        // Handle drop (button held)
        // Button state updated at start of loop

        // Auto drop
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        uint32_t interval = is_button_held() ? 50 : drop_interval;

        if (now - last_drop > interval) {
            last_drop = now;

            if (!check_collision(current_piece, current_rotation, piece_x, piece_y + 1)) {
                piece_y++;
            } else {
                lock_piece();
                play_drop_sound();

                int cleared = clear_lines();
                if (cleared > 0) {
                    lines += cleared;
                    score += cleared * cleared * 100 * level;
                    level = (lines / 10) + 1;
                    drop_interval = 1000 - (level - 1) * 100;
                    if (drop_interval < 100) drop_interval = 100;
                    play_line_clear_sound();
                }

                spawn_piece();
                if (game_over) {
                    play_game_over_sound();
                }
            }
        }

        update_display();
        vTaskDelay(pdMS_TO_TICKS(16));  // ~60 FPS
    }
}
