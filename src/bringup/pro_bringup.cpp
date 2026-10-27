// T-Deck Pro bring-up (OS milestone 0). Built only by env:tdeck-pro until the Pro has
// its own display and input drivers. It runs the board layer, prints the banner, checks
// that every part answers, and prints a pass/fail line. Send 'b' over serial to repeat.
//
// Nothing is transmitted on the radio and the modem stays off.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "board.h"

static bool sx1262_version(char *ver, size_t n)
{
    if (!board_spi_lock(1000)) return false;
    digitalWrite(RADIO_RST_PIN, LOW); delay(2); digitalWrite(RADIO_RST_PIN, HIGH); delay(20);
    uint32_t t = millis();
    while (digitalRead(RADIO_BUSY_PIN) && millis() - t < 200) delay(1);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(RADIO_CS_PIN, LOW);
    SPI.transfer(0x1D); SPI.transfer(0x03); SPI.transfer(0x20); SPI.transfer(0x00);  // ReadRegister 0x0320
    for (size_t k = 0; k + 1 < n && k < 16; k++) {
        char c = (char)SPI.transfer(0x00);
        ver[k] = (c < 32 || c > 126) ? '.' : c;
        ver[k + 1] = 0;
    }
    digitalWrite(RADIO_CS_PIN, HIGH);
    SPI.endTransaction();
    board_spi_unlock();
    return strncmp(ver, "SX126", 5) == 0;
}

// Powers the GPS for a few seconds, looks for NMEA at 38400, then powers it off again.
static bool gps_answers()
{
    digitalWrite(BOARD_GPS_EN, HIGH);
    Serial2.begin(BOARD_GPS_BAUD, SERIAL_8N1, BOARD_GPS_RX_PIN, BOARD_GPS_TX_PIN);
    uint32_t t = millis();
    bool seen = false;
    char prev = 0;
    while (!seen && millis() - t < 4000) {
        while (Serial2.available()) {
            char c = (char)Serial2.read();
            if (prev == '$' && c == 'G') seen = true;
            prev = c;
        }
        delay(5);
    }
    Serial2.end();
    digitalWrite(BOARD_GPS_EN, LOW);
    return seen;
}

static bool i2c_present(uint8_t a)
{
    Wire.beginTransmission(a);
    return Wire.endTransmission() == 0;
}

static void check(const char *name, bool ok, int &fails)
{
    Serial.printf("[CHECK] %-22s %s\n", name, ok ? "ok" : "MISSING");
    if (!ok) fails++;
}

static void run_checks()
{
    board_print_banner(Serial);
    int fails = 0;
    check("revision known", board_rev() != BOARD_REV_PRO_UNKNOWN, fails);
    check("PSRAM", ESP.getPsramSize() > 0, fails);
    check("touch 0x1A", i2c_present(0x1A), fails);
    check("IMU 0x28", i2c_present(0x28), fails);
    check("keyboard 0x34", i2c_present(KEYBOARD_I2C_ADDR), fails);
    check("fuel gauge 0x55", i2c_present(0x55), fails);
    check("charger 0x6B", i2c_present(0x6B), fails);
    char ver[17] = "";
    bool radio = sx1262_version(ver, sizeof ver);
    Serial.printf("[CHECK] radio version \"%s\"\n", ver);
    check("SX1262", radio, fails);
    check("GPS NMEA at 38400", gps_answers(), fails);
    Serial.println("[CHECK] modem                  off by default (not powered)");
    Serial.printf("[BRINGUP] %s (%d missing)\n", fails ? "FAIL" : "PASS", fails);
}

void setup()
{
    Serial.begin(115200);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000) delay(10);
    board_early_init();
    run_checks();
}

void loop()
{
    if (Serial.available() && Serial.read() == 'b') run_checks();
    delay(20);
}
