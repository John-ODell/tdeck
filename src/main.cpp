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

    power_init();
    display_init();
    input_init();

    ui_register_app(&app_about);
    ui_init();

    lv_timer_handler();
    display_set_brightness(12);
}

void loop()
{
    input_poll();
    ui_tick();
    lv_timer_handler();
    delay(2);
}
