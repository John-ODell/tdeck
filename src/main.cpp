#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <lvgl.h>
#include "board.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/power.h"
#include "ui/ui.h"
#include "apps/apps.h"
#include <LittleFS.h>
#include "core/settings.h"
#include "core/nodedb.h"
#include "core/messages.h"
#include "mesh/mesh.h"
#include "hal/gps.h"
#include "core/storage.h"
#include "core/console.h"
#include "link/companion.h"

static uint32_t s_wake_at = 0;
void screen_wake() { s_wake_at = millis(); }
static bool s_screen_off = false;

static void on_text(const Message &m)
{
    s_wake_at = millis();
    uint32_t peer = messages::peer_of(m, mesh::our_num());
    if (chat_viewing(peer)) return;
    char name[44], buf[120];
    nodedb::display_name(m.from, name, sizeof(name));
    snprintf(buf, sizeof(buf), "%s%s: %s", peer == 0xFFFFFFFF ? "" : "DM ", name, m.text);
    ui_toast(buf);
}

static void update_unread()
{
    static size_t last = (size_t)-1;
    size_t n = messages::unread_count();
    if (n == last) return;
    last = n;
    char buf[24] = "";
    if (n) snprintf(buf, sizeof(buf), LV_SYMBOL_ENVELOPE " %u", (unsigned)n);
    ui_set_status_extra(buf);
}

// Backlight off after the idle timeout; any input or a new message wakes it
static void manage_screen()
{
    uint32_t last = input_last_activity_ms();
    if ((int32_t)(s_wake_at - last) > 0) last = s_wake_at;
    if (s_screen_off) {
        if (millis() - last < 1000) {
            s_screen_off = false;
            display_set_brightness(settings.brightness);
            input_set_screen_off(false);
        }
    } else if (settings.screen_timeout_s && millis() - last > settings.screen_timeout_s * 1000UL) {
        s_screen_off = true;
        input_set_screen_off(true);
        display_set_brightness(0);
    }
}

void setup()
{
    Serial.begin(115200);

    // Peripheral rail first, then park every SPI chip select before the bus starts
    pinMode(BOARD_POWERON, OUTPUT);
    digitalWrite(BOARD_POWERON, HIGH);
    pinMode(BOARD_SDCARD_CS, OUTPUT);
    pinMode(RADIO_CS_PIN, OUTPUT);
    pinMode(BOARD_TFT_CS, OUTPUT);
    digitalWrite(BOARD_SDCARD_CS, HIGH);
    digitalWrite(RADIO_CS_PIN, HIGH);
    digitalWrite(BOARD_TFT_CS, HIGH);
    pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    // Keyboard MCU needs a moment after the rail comes up
    delay(200);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

    if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) Serial.println("LittleFS mount failed");
    settings_load();
    apply_timezone();
    nodedb::load();
    messages::load();

    power_init();
    display_init();
    input_init();

    ui_register_app(&app_chat);
    ui_register_app(&app_nodes);
    ui_register_app(&app_map);
    ui_register_app(&app_gps);
    ui_register_app(&app_wifi);
    ui_register_app(&app_ble);
    ui_register_app(&app_spectrum);
    ui_register_app(&app_keyboard);
    ui_register_app(&app_notes);
    ui_register_app(&app_timer);
    ui_register_app(&app_music);
    ui_register_app(&app_todo);
    ui_register_app(&app_files);
    ui_register_app(&app_settings);
    ui_register_app(&app_about);
    ui_init();

    lv_timer_handler();
    display_set_brightness(settings.brightness);

    if (!mesh::init()) ui_toast("LoRa radio not found");
    mesh::set_text_callback(on_text);
    sd_mount();
    gps_init();
    companion::init();
}

void loop()
{
    input_poll();
    mesh::loop();
    gps_loop();
    update_unread();
    companion::tick();
    music_loop();
    ui_tick();
    console_poll();
    manage_screen();
    lv_timer_handler();
    delay(s_screen_off ? 10 : 2);
}
