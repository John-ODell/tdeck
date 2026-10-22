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

static void on_text(const Message &m)
{
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
    ui_register_app(&app_about);
    ui_init();

    lv_timer_handler();
    display_set_brightness(settings.brightness);

    if (!mesh::init()) ui_toast("LoRa radio not found");
    mesh::set_text_callback(on_text);
    sd_mount();
    gps_init();
}

void loop()
{
    input_poll();
    mesh::loop();
    gps_loop();
    update_unread();
    ui_tick();
    lv_timer_handler();
    delay(2);
}
