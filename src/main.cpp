#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <lvgl.h>
#include "board.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/audio.h"
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
static uint32_t s_fade_start = 0;    // backlight fading towards the lock

// Locked = screen off and every key or roll ignored, so a pocket press changes nothing.
// A message or alarm still lights the screen for a moment but keeps the lock.
static void set_lock(bool lock)
{
    input_set_locked(lock);
    s_fade_start = 0;
    s_screen_off = lock;
    display_set_brightness(lock ? 0 : settings.brightness);
    if (!lock) s_wake_at = millis();
}
void screen_lock() { set_lock(true); }
void screen_unlock() { set_lock(false); }

static void on_text(const Message &m)
{
    s_wake_at = millis();
    uint32_t peer = messages::peer_of(m, mesh::our_num());
    if (chat_viewing(peer)) return;
    if (settings.chat_tone) {
        if (peer == 0xFFFFFFFF) { audio_tone(1760, 60); audio_tone(0, 40); audio_tone(2349, 90); } // channel: a short chirp
        else {                                                                                      // DM: a longer rising ring
            audio_tone(1319, 90); audio_tone(0, 30); audio_tone(1760, 90); audio_tone(0, 30); audio_tone(2217, 160);
            audio_tone(0, 120); audio_tone(2217, 160);
        }
    }
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

// Backlight fades out after the idle timeout and the T-Deck locks. Input while locked is
// dropped (and is not activity), so only screen_wake() from a message or alarm, or the
// trackball hold through the lock hook, lights it again.
static void manage_screen()
{
    const uint32_t FADE_MS = 800;
    uint32_t last = input_last_activity_ms();
    if ((int32_t)(s_wake_at - last) > 0) last = s_wake_at;
    // Input on a lit, locked screen is dropped: say so instead of looking dead (#161)
    static uint32_t blocked_seen = 0, hint_at = 0;
    uint32_t blocked = input_blocked_count();
    if (blocked != blocked_seen) {
        blocked_seen = blocked;
        if (!s_screen_off && millis() - hint_at > 2000) { hint_at = millis(); ui_toast("Locked: hold the trackball 1 s"); }
    }
    if (s_screen_off) {
        if (millis() - s_wake_at < 1000) {
            s_screen_off = false;              // lit for a look; still locked
            display_set_brightness(settings.brightness);
        }
    } else if (settings.screen_timeout_s && millis() - last > settings.screen_timeout_s * 1000UL) {
        if (!s_fade_start) s_fade_start = millis();
        uint32_t t = millis() - s_fade_start;
        if (t >= FADE_MS) set_lock(true);
        else display_set_brightness(settings.brightness * (FADE_MS - t) / FADE_MS);
    } else if (s_fade_start) {
        s_fade_start = 0;                      // touched during the fade
        display_set_brightness(settings.brightness);
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
    audio_tone_level(settings.tone_volume);
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
    ui_register_app(&app_compass);
    ui_register_app(&app_wifi);
    ui_register_app(&app_ble);
    ui_register_app(&app_spectrum);
    ui_register_app(&app_keyboard);
    ui_register_app(&app_notes);
    ui_register_app(&app_todo);
    ui_register_app(&app_timer);
    ui_register_app(&app_music);
    ui_register_app(&app_files);
    ui_register_app(&app_settings);
    ui_register_app(&app_about);
    ui_init();

    lv_timer_handler();
    display_set_brightness(settings.brightness);

    if (!mesh::init()) ui_toast("LoRa radio not found");
    mesh::set_text_callback(on_text);
    input_set_lock_hook(set_lock);
    sd_mount();
    gps_init();
    companion::init();
    trip_init();
}

void loop()
{
    input_poll();
    mesh::loop();
    gps_loop();
    update_unread();
    companion::tick();
    music_loop();
    audio_tick();
    ui_tick();
    console_poll();
    manage_screen();
    lv_timer_handler();
    delay(s_screen_off ? 10 : 2);
}
