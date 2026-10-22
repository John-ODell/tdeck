# LilyGO T-Deck Plus (915 MHz, internal antenna) — Hardware Pin Map and Bring-up Guide

Source conventions:
- LOCAL = upstream LilyGO clone at `.../scratchpad/upstream/T-Deck`, HEAD commit `1dddf6e0d3aaa74cd4e10ba02938865b99682391` (2026-09-29). Its GitHub equivalent is https://github.com/Xinyuan-LilyGO/T-Deck (paths below are relative to repo root, so the GitHub link is `https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/<path>`).
- MT = Meshtastic firmware `develop` branch, fetched 2026-10-22: https://github.com/meshtastic/firmware/tree/develop/variants/esp32s3/t-deck
- The firmware has one shared "T-Deck" target and no separate Plus-only pin map. Both LilyGO and Meshtastic use the same pins for T-Deck and T-Deck Plus. On the Plus, the Grove port pins (43/44) are used by the internal GPS.

## Q1. Complete GPIO map (ESP32-S3FN16R8)

### Takeaway
LilyGO and Meshtastic give the same GPIO numbers for every peripheral. There are two differences. GPIO46 is the keyboard interrupt in LilyGO's file but is labelled "KB_BL_PIN, not used" in Meshtastic. Meshtastic also uses 2.11 as the battery ADC multiplier, where LilyGO uses 2.0. GPIO10 is the main power enable for all peripherals and must be driven HIGH first.

### Cited Findings
Pin table from `examples/UnitTest/utilities.h` (LOCAL; https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/utilities.h), checked against MT `variant.h` (https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h):

| Function | GPIO | LilyGO macro | Meshtastic macro | Agree? |
|---|---|---|---|---|
| Peripheral power enable (must be HIGH) | 10 | BOARD_POWERON | KB_POWERON | yes |
| SPI SCK (TFT/LoRa/SD shared) | 40 | BOARD_SPI_SCK | SPI_SCK / LORA_SCK / ST7789_SCK | yes |
| SPI MOSI | 41 | BOARD_SPI_MOSI | SPI_MOSI / ST7789_SDA | yes |
| SPI MISO | 38 | BOARD_SPI_MISO | SPI_MISO / ST7789_MISO | yes |
| TFT CS | 12 | BOARD_TFT_CS | TFT_CS | yes |
| TFT DC | 11 | BOARD_TFT_DC | ST7789_RS | yes |
| TFT RST | none (-1) | TFT_RST -1 (Setup210) | ST7789_RESET -1 | yes |
| TFT backlight | 42 | BOARD_TFT_BACKLIGHT / BOARD_BL_PIN | TFT_BL / ST7789_BL | yes |
| LoRa CS (NSS) | 9 | RADIO_CS_PIN | LORA_CS | yes |
| LoRa RST | 17 | RADIO_RST_PIN | LORA_RESET | yes |
| LoRa BUSY | 13 | RADIO_BUSY_PIN | LORA_DIO2 / SX126X_BUSY (named "DIO2" but it is BUSY) | yes |
| LoRa DIO1 (IRQ) | 45 | RADIO_DIO1_PIN | LORA_DIO1 | yes |
| SD card CS | 39 | BOARD_SDCARD_CS | SPI_CS / SDCARD_CS | yes |
| I2C SDA | 18 | BOARD_I2C_SDA | I2C_SDA | yes |
| I2C SCL | 8 | BOARD_I2C_SCL | I2C_SCL | yes |
| Touch (GT911) INT | 16 | BOARD_TOUCH_INT | SCREEN_TOUCH_INT | yes |
| Keyboard (ESP32-C3) INT | 46 | BOARD_KEYBOARD_INT | KB_BL_PIN 46 "not used for now" | **CONFLICT in label** |
| Trackball G01 = UP | 3 | BOARD_TBOX_G01 | TB_UP 3 | yes |
| Trackball G02 = RIGHT | 2 | BOARD_TBOX_G02 | TB_RIGHT 2 | yes |
| Trackball G03 = DOWN | 15 | BOARD_TBOX_G03 | TB_DOWN 15 | yes |
| Trackball G04 = LEFT | 1 | BOARD_TBOX_G04 | TB_LEFT 1 | yes |
| Trackball click = BOOT | 0 | BOARD_BOOT_PIN | TB_PRESS 0 | yes |
| Battery ADC | 4 (ADC1_CH3) | BOARD_BAT_ADC | BATTERY_PIN 4, ADC_CHANNEL_3 | yes |
| GPS: ESP RX (from GPS TX) | 44 | BOARD_GPS_RX_PIN | GPS_RX_PIN 44 | yes |
| GPS: ESP TX (to GPS RX) | 43 | BOARD_GPS_TX_PIN | GPS_TX_PIN 43 | yes |
| Speaker I2S WS (LRCLK) | 5 | BOARD_I2S_WS | DAC_I2S_WS | yes |
| Speaker I2S BCK | 7 | BOARD_I2S_BCK | DAC_I2S_BCK | yes |
| Speaker I2S DOUT | 6 | BOARD_I2S_DOUT | DAC_I2S_DOUT | yes |
| Mic ES7210 MCLK | 48 | BOARD_ES7210_MCLK | ES7210_MCLK | yes |
| Mic ES7210 LRCK | 21 | BOARD_ES7210_LRCK | ES7210_LRCK (MT also sets DAC_I2S_MCLK 21) | yes |
| Mic ES7210 SCK | 47 | BOARD_ES7210_SCK | ES7210_SCK | yes |
| Mic ES7210 DIN (data into ESP) | 14 | BOARD_ES7210_DIN | ES7210_DIN | yes |

- Source code comment: "The board peripheral power control pin needs to be set to HIGH when using the peripheral" (`#define BOARD_POWERON 10`) — [LOCAL examples/UnitTest/utilities.h](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/utilities.h)
- Meshtastic `earlyInitVariant()` comment: GPIO10 "manages all peripheral power supplies", which are the ESP32-C3 keyboard, MAX98357A amp, TF card, display backlight (AW9364DNR), AN48841B trackball and ES7210. It also warns: "If some boards are turned on late, ESP32 will reset due to low voltage." — [MT variant.cpp](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.cpp)
- The trackball directions come from how the UnitTest `mouse_read()` handles each pin. Pin 2 (G02) moves x+ (right), pin 3 (G01) moves y- (up), pin 1 (G04) moves x- (left), pin 15 (G03) moves y+ (down) and GPIO0 is the click. Each pin is read by detecting edges (toggles) with `INPUT_PULLUP`. Sensitivity is `trackball_sensitivity = 24` px per toggle — [LOCAL examples/UnitTest/UnitTest.ino lines ~1130-1175, 881-884](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- Meshtastic trackball: `TB_DIRECTION FALLING`, `TB_THRESHOLD 3`, `BUTTON_ACTIVE_LOW true`, `BUTTON_ACTIVE_PULLUP true`. The t-deck-tft env sets `INPUTDRIVER_ENCODER_LEFT=1 RIGHT=2 UP=3 DOWN=15 BTN=0` — [MT variant.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h), [MT platformio.ini](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/platformio.ini)
- Meshtastic `pins_arduino.h`: TX=43, RX=44, SDA=18, SCL=8, SS=9 ("Default SPI will be mapped to Radio"), MOSI=41, MISO=38, SCK=40, BAT_ADC_PIN=4, USB VID/PID 0x303a/0x1001 — [MT pins_arduino.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/pins_arduino.h)
- The schematic text includes the nets `C3_INT`, `GPIO46`, `ESP_TP_INT`, `BAT_ADC`, `PWR_EN`, `ESP_SD_CS`, `Lora_BUSY_GPIO13`, `Lora_DIO1_GPIO45`, `Lora_NRSET_GPIO17`, and the parts `AW9364DNR`, `TP4065B`, `MAX98357A`, `ES7210` — [LOCAL schematic/schematic.pdf](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/schematic/schematic.pdf) (text extracted with pdftotext)
- I2C addresses on the shared bus (SDA 18 / SCL 8): keyboard C3 = 0x55, GT911 = 0x5D (or 0x14), ES7210 = 0x40 (`ES7210_AD1_AD0_00 = 0x40`) — [LOCAL lib/es7210/src/es7210.h](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/lib/es7210/src/es7210.h), [LOCAL UnitTest.ino](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- README: "If a microphone is used, the GPIO0 button cannot be used" — [LOCAL README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/README.MD)
- README: "T-Deck-Plus has assigned the pins on the Grove interface to the GPS module, so the Grove interface of T-Deck-Plus cannot be used." — [LOCAL README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/README.MD)

### Inferences
- GPIO46 is on the schematic net `C3_INT`/`GPIO46`, so it is most likely the keyboard's "key available" interrupt. The Meshtastic name "KB_BL_PIN" looks wrong, since the keyboard backlight is on the C3's own GPIO9. Neither LilyGO nor Meshtastic actually uses GPIO46 in code (both poll over I2C). GPIO46 is also an ESP32-S3 strapping pin (input-only during boot), so leave it as an input.
- GPIO0 (trackball click) doubles as the BOOT strap and as a deep-sleep wake source.
- GPIO43/44 are UART0 on the ESP32-S3. With `ARDUINO_USB_CDC_ON_BOOT=1`, `Serial` is USB-CDC, so UART0 pins are free for `Serial1` to the GPS.

### Gaps
- The schematic shows a net labelled `Lora_P1.01_GPIO2` near the LoRa module. GPIO2 is also trackball RIGHT, and I could not work out from the extracted text what this net means. It may be an NC module pad. Check visually in the schematic PDF.
- The trackball sensor IC (AN48841B, per the MT comment) is not in the LilyGO pin file.

## Q2. SX1262 LoRa specifics (TCXO, RF switch, current, power)

### Takeaway
LoRa is an SX1262 on the shared SPI bus with CS 9, RST 17, BUSY 13 and DIO1 45. DIO2 drives the RF switch and DIO3 powers the TCXO. **The TCXO voltage differs between sources.** Meshtastic sets it to 1.8 V. LilyGO's UnitTest calls `radio.begin()` with the RadioLib default of 1.6 V. LilyGO sets the current limit to 140 mA and TX power to 22 dBm. There are no separate RF-switch GPIOs.

### Cited Findings
- LilyGO creates the module as `SX1262 radio = new Module(RADIO_CS_PIN /*9*/, RADIO_DIO1_PIN /*45*/, RADIO_RST_PIN /*17*/, RADIO_BUSY_PIN /*13*/);` then calls `radio.begin()` with no arguments — [LOCAL UnitTest.ino ~L111, L321](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- The bundled RadioLib 7.1.0 `SX1262::begin(float freq = 434.0, float bw = 125.0, uint8_t sf = 9, uint8_t cr = 7, uint8_t syncWord = PRIVATE, int8_t power = 10, uint16_t preambleLength = 8, float tcxoVoltage = 1.6, bool useRegulatorLDO = false)`. Default "TCXO reference voltage to be set on DIO3. Defaults to 1.6 V." — [LOCAL lib/RadioLib/src/modules/SX126x/SX1262.h L41-47](https://github.com/jgromes/RadioLib/blob/master/src/modules/SX126x/SX1262.h)
- LilyGO UnitTest radio settings: setFrequency(RADIO_FREQ; default 868.0, 920.0 if JAPAN_MIC), setBandwidth(125.0), setSpreadingFactor(10), setCodingRate(6), setSyncWord(0xAB), setOutputPower(22; 7 if JAPAN_MIC), **setCurrentLimit(140)**, setPreambleLength(15), setCRC(false), setDio1Action(setFlag) — [LOCAL UnitTest.ino L337-393](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino), [LOCAL utilities.h](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/utilities.h)
- Meshtastic: `USE_SX1262`, `USE_SX1268`, `LORA_DIO0 -1`, `SX126X_DIO2_AS_RF_SWITCH`, `SX126X_DIO3_TCXO_VOLTAGE 1.8`. Comments: "Internally the TTGO module hooks the SX1262-DIO2 in to control the TX/RX switch", and DIO3 is "Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled" — [MT variant.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h)
- If begin() fails, LilyGO's UnitTest treats it as "T-Deck without LoRa" and reinitialises SPI — [LOCAL UnitTest.ino L321-330](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- Product page: SX1262, +22 dBm. SKU variants include "915 MHz [H737-A]" without an external antenna and "915 MHz [H737-03]" with one — [LilyGO T-Deck Plus product page](https://lilygo.cc/products/t-deck-plus-meshtastic)
- CNX Software: Plus LoRa has a "built-in internal antenna and supports an external antenna" — [CNX Software](https://cnx-software.com/?p=136193)
- Meshtastic docs warn "Do not proceed unless an antenna is connected" before flashing (generic T-Deck page) — [Meshtastic T-Deck docs](https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/)

### Inferences
- For your own firmware, `radio.begin(915.0, 125.0, 9, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 22, 8, 1.8, false)` followed by `radio.setDio2AsRfSwitch(true)` and `radio.setCurrentLimit(140)` follows Meshtastic's proven configuration. RadioLib's `begin()` already enables DIO2 as the RF switch by default in recent versions, but calling it explicitly does no harm. LilyGO's use of 1.6 V also works in practice, because SX126x TCXOs usually tolerate 1.6–1.8 V. Choose 1.8 V to match Meshtastic, which runs on a very large number of these units.
- On the internal-antenna SKU (H737-A), the antenna is permanently connected, so the Meshtastic "no antenna" warning applies mainly to SKUs with an external connector. 22 dBm into the internal antenna is the configuration LilyGO ships.
- 140 mA is the RadioLib maximum for SX1262 OCP, so LilyGO is setting the highest current limit.

### Gaps
- No source gives the internal antenna's gain or efficiency. `datasheet/T-Deck-Plus-Antenna.pdf` is image-only, so no text could be extracted.
- The exact LoRa module part number (S62F/HPD16A-style TTGO module?) is not stated. "HPD16A" appears in the schematic text, but it is unclear which part it labels.
- I found no regulator mode recommendation (DC-DC vs LDO). Both sources use the RadioLib default (DC-DC, `useRegulatorLDO=false`).

## Q3. ST7789 display configuration

### Takeaway
The display is a 240x320 ST7789 panel in landscape (rotation 1) at 320x240. There is no reset pin. Colors are inverted (INVON 0x21) with RGB order. SPI runs at 40 MHz write and 16–20 MHz read. Backlight GPIO42 drives an AW9364 driver that takes a 16-step pulse count, not PWM. LilyGO uses pulse counting; Meshtastic/LovyanGFX drives the same pin with PWM (44.1 kHz, channel 7).

### Cited Findings
- TFT_eSPI setup used: `User_Setup_Select.h` includes `User_Setups/Setup210_LilyGo_T_Deck.h` — "For the LilyGo T-Deck based ESP32S3 with ST7789 320 x 240 TFT" — [LOCAL lib/TFT_eSPI/User_Setup_Select.h L137](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/lib/TFT_eSPI/User_Setup_Select.h)
- Setup210: `ST7789_DRIVER`, `TFT_WIDTH 240`, `TFT_HEIGHT 320`, `TFT_RGB_ORDER TFT_RGB`, `INIT_SEQUENCE_2` ("Different gamma values"), MISO 38, MOSI 41, SCLK 40, CS 12, DC 11, RST -1, `TFT_BACKLIGHT_ON 1`, `TFT_BL` commented out ("Turn off backlight function by default"), `SPI_FREQUENCY 40000000`, `SPI_READ_FREQUENCY 20000000` — [LOCAL lib/TFT_eSPI/User_Setups/Setup210_LilyGo_T_Deck.h](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/lib/TFT_eSPI/User_Setups/Setup210_LilyGo_T_Deck.h)
- UnitTest calls `tft.begin(); tft.setRotation(1);`. LVGL `hor_res = TFT_HEIGHT (320)`, `ver_res = TFT_WIDTH (240)`, `full_refresh = 1`, and the draw buffer is `TFT_WIDTH*TFT_HEIGHT*sizeof(lv_color_t)` allocated with `ps_malloc` (PSRAM) — [LOCAL UnitTest.ino](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- UnitTest contains an optional (`#if 0`) ST7789V override init sequence. Comment: "T-Deck-Plus and T-Deck display panels are different… used when the display is abnormal after the TFT_eSPI update". Sequence: 0x01 (+120 ms), 0x11 (+120 ms), 0x3A=0x05, 0x36=0x55, 0xB2={0C,0C,00,33,33}, 0xB7=0x75, 0xBB=0x1A, 0xC0=0x2C, 0xC2=0x01, 0xC3=0x13, 0xC4=0x20, 0xC6=0x0F, 0xD0={A4,A1}, 0xD6=0xA1, 0xE0={D0,0D,14,0D,0D,09,38,44,4E,3A,17,18,2F,30}, 0xE1={D0,09,0F,08,07,14,37,44,4D,38,15,16,2C,3E}, **0x21 (invertDisplay)**, 0x29, 0x2C — [LOCAL UnitTest.ino L67-91, L897-911](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- README: "T-Deck updated TFT_eSPI ST7789 initialization sequence on 20240726… not been pushed to the TFT_eSPI upstream branch. If… display is incorrect… check… consistent with the initialization sequence in the warehouse." It also says TFT_eSPI does not work on Arduino-ESP32 > 2.0.14 (TFT_eSPI issue 3329) — [LOCAL README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/README.MD)
- Backlight (LilyGO): "LilyGo T-Deck control backlight chip has 16 levels of adjustment range… 0~16". `setBrightness()`: 0 means hold LOW for at least 3 ms (off). From off, drive HIGH and wait 30 µs (this sets level 16). Each LOW→HIGH pulse then steps down one level, and the number of pulses is `(16 + (16-value) - (16-level)) % 16` — [LOCAL UnitTest.ino L276-300](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- The backlight chip is AW9364DNR — [MT variant.cpp](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.cpp), [LOCAL schematic.pdf](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/schematic/schematic.pdf)
- Meshtastic LovyanGFX config (LGFX_T_DECK.h): `spi_host SPI2_HOST`, `spi_mode 0`, `freq_write SPI_FREQUENCY (40 MHz)`, `freq_read 16000000`, `dma_channel SPI_DMA_CH_AUTO`, pins 40/41/38/DC 11/CS 12/RST -1, `offset_rotation 1`, `invert true`, `rgb_order false`, `readable true`, `bus_shared true`. Backlight: `pin_bl 42`, `invert false`, `freq 44100`, `pwm_channel 7` — [MT/device-ui LGFX_T_DECK.h](https://github.com/meshtastic/device-ui/blob/master/include/graphics/LGFX/LGFX_T_DECK.h)
- MT variant: `SPI_FREQUENCY 40000000`, `SPI_READ_FREQUENCY 16000000`, `BRIGHTNESS_DEFAULT 130`, `SCREEN_ROTATE`, `TFT_OFFSET_ROTATION 0`, env flags `DISPLAY_SIZE=320x240 ; landscape mode`, `LGFX_BUFSIZE=153600` — [MT variant.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h), [MT platformio.ini](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/platformio.ini)

### Inferences
- Read speeds differ between sources (TFT_eSPI 20 MHz, LGFX 16 MHz). Both are safe, and read only matters for `readPixel`.
- Panel configuration: native 240x320 portrait, rotation 1 for 320x240 landscape, color inversion ON, RGB order, RGB565 (COLMOD 0x05).
- Backlight: use the AW9364 pulse-count method for exact 16 levels. Meshtastic's PWM at 44.1 kHz evidently also works on this chip. The LilyGO pulse method is the vendor-documented one.
- `TFT_BL` is undefined in Setup210, so TFT_eSPI does not touch GPIO42. Your code must drive it, otherwise the screen stays dark.

### Gaps
- No source gives a datasheet-backed reason why LilyGO uses pulse counting while Meshtastic's PWM also works.

## Q4. Touch controller (GT911) and the "TouchFix" firmware

### Takeaway
The GT911 is on I2C (18/8) with INT on GPIO16 and no reset pin. It answers at 0x5D (seen in the TouchFix log) or 0x14, depending on the INT level at power-up, so probe both. Mapping for landscape: max coords 320x240, swapXY = true, mirror (false, true). TouchFix is a LilyGO repair image for T-Deck **Plus** (rounded-corner screen) units whose touch does not work. Only flash it on rounded-corner screens.

### Cited Findings
- UnitTest: `touch.setPins(-1, BOARD_TOUCH_INT); touch.begin(Wire); touch.setMaxCoordinates(320, 240); touch.setSwapXY(true); touch.setMirrorXY(false, true);` (SensorLib TouchDrvGT911) — [LOCAL UnitTest.ino L917-935](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- SensorLib constants `GT911_SLAVE_ADDRESS_H (0x14)` and `GT911_SLAVE_ADDRESS_L (0x5D)`. The driver probes both: `device_address[2] = {GT911_SLAVE_ADDRESS_L, GT911_SLAVE_ADDRESS_H}`, logging "GT911 using 0x28 address!" / "GT911 using 0xBA address!" (these are the 8-bit write forms 0x14<<1 and 0x5D<<1) — [LOCAL lib/SensorsLib/src/TouchDrvGT911.hpp L533-583, REG/GT911Constants.h L33-34](https://github.com/lewisxhe/SensorLib)
- Coordinator-supplied data point (strings in prebuilt `firmware/T-Deck-Plus-TouchFix_241203.bin`): the image was built with framework-arduinoespressif32@3.20009.0 (ESP-IDF v4.4.4, i.e. Arduino-ESP32 2.0.9) and SensorLib TouchDrvGT911, and it probes the GT911 at both 0x28 and 0xBA. I did not check this myself; it matches the SensorLib code above.
- firmware/README.MD: "T-Deck-Plus-TouchFix_241025.bin Touch repair program, can only be flashed on rounded corner screens, please do not flash this on T-Deck rectangular screens, otherwise the screen touch will fail". The section is titled "Used to try to repair the touch when the touch does not work properly". The expected log is `Probe address is : 0x5D`, `Product id:911`, `Current version char :41`, `WriteConfig version char :0`, `Firmware version: 0x1060`, `Resolution : X = 240 Y = 320`, `Vendor id:57`, `Refresh Rate:10 ms`, `MaxTouchPoint:5`, `Interrupt Mode: FALLING`, I2C at `sda=18 scl=8 freq=100000`. Files: TouchFix_241025.bin and TouchFix_241203.bin. It links a YouTube "T-Deck Plus Touch Fix Record" and a Reddit thread "tdeck_touchscreen_not_working_properly" — [LOCAL firmware/README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/firmware/README.MD)
- Meshtastic: `TOUCH_SLAVE_ADDRESS 0x5D`, `SCREEN_TOUCH_INT 16`, `TOUCH_I2C_PORT 0`. The LGFX GT911 wrapper defines `GT911_I2C_ADDR_1 = 0x14`, `GT911_I2C_ADDR_2 = 0x5D` and retries the other address if a transaction fails. Config: `i2c_addr 0x5D`, `freq 400000`, `pin_int 16`, `bus_shared true`, `x_max = screenWidth-1`, `y_max = screenHeight-1` — [MT variant.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h), [device-ui LGFX_T_DECK.h](https://github.com/meshtastic/device-ui/blob/master/include/graphics/LGFX/LGFX_T_DECK.h)

### Inferences
- The log line "WriteConfig version char :0" after reading config version 0x41 (65) suggests TouchFix rewrites the GT911 configuration block, starting from version 0, with the correct 240x320 resolution and settings. It probably works by restoring a GT911 config register area (0x8047…) that was corrupted or misprogrammed at the factory. This is an inference from the log only; LilyGO does not publish the source.
- With no RST pin (`setPins(-1, 16)`), you cannot force the address with the standard INT/RST sequence, so probe 0x5D first and then 0x14. GPIO16 is the INT input. Some drivers briefly drive INT during reset, which you cannot do here without RST.
- The rounded-corner vs rectangular warning means the Plus uses a different panel/touch module from the original T-Deck, with a different GT911 config. Never write a GT911 config from your own firmware unless you need to.

### Gaps
- The Reddit thread could not be fetched (blocked). The exact root cause of the touch failure (lost GT911 config vs. hardware) is not documented in any source I could reach.
- The difference between TouchFix 241025 and 241203 is undocumented.

## Q5. Keyboard (ESP32-C3 "T-Keyboard") I2C protocol

### Takeaway
The keyboard is an I2C slave at 0x55 (100 kHz on the C3 side). Read 1 byte: 0x00 = no key, otherwise an ASCII char. Enter = 0x0D, Backspace = 0x08, Alt+C = 0x0C. Alt+B toggles the backlight inside the C3 and sends nothing. Write commands: 0x01 <duty 0-255> sets the backlight PWM, 0x02 <duty >30> sets the Alt+B default, 0x03 switches to raw matrix mode (5 bytes, one per column, bitmask of 7 rows), and 0x04 switches back to key mode.

### Cited Findings
- C3 firmware: `I2C_DEV_ADDR 0x55`, `keyboard_BL_PIN 9`, C3 SDA 2 / SCL 10, `Wire.begin(0x55, SDA, SCL, 100000UL)`, backlight LEDC ch 0, 1000 Hz, 8-bit, boot duty 0, `KB_BRIGHTNESS_DEFAULT_DUTY 127` — [LOCAL examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino L18-26, L209-217](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino)
- Commands: `LILYGO_KB_BRIGHTNESS_CMD 0x01`, `LILYGO_KB_ALT_B_BRIGHTNESS_CMD 0x02`, `LILYGO_KB_MODE_RAW_CMD 0x03`, `LILYGO_KB_MODE_KEY_CMD 0x04`. 0x02 only accepts duty > 30 — [same file L29-32, L73-108](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino)
- `onRequest()`: in raw mode it writes one byte per column (5 columns, bit n = row n, 7 rows). In key mode it sends `comdata` if one is pending, otherwise `(char)0x00` — [same file L274-297](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino)
- Special keys: key(3,3) Enter → 0x0D; key(4,3) backspace → 0x08; Alt(0,4)+B(3,4) toggles the backlight, using the user duty or the Alt+B default if the user duty is 0, and sets `comdata_flag=false` (no char sent); Alt+C → 0x0C. Matrix: rows (C3 GPIO) {0,3,19,12,18,6,7}, cols {1,4,5,11,13}. A symbol layer (`keyboard_symbol[][]`) maps keys to digits and symbols — [same file L33-37, L237-271](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_ESP32C3/Keyboard_ESP32C3.ino)
- S3 host side: probe with `Wire.beginTransmission(0x55)`/`endTransmission()`, then `Wire.requestFrom(0x55, 1)`. Brightness is set with `Wire.write(0x01); Wire.write(level);` — [LOCAL UnitTest.ino L1205-1220, L1405-1419](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino); also [LOCAL examples/Keyboard_T_Deck_Master](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/Keyboard_T_Deck_Master/Keyboard_T_Deck_Master.ino)
- Meshtastic: `KB_SLAVE_ADDRESS TDECK_KB_ADDR // 0x55`, `INPUTDRIVER_I2C_KBD_TYPE=0x55`. Docs: "Press Alt+B to toggle the backlight… works in both Base UI and MUI because the keyboard's own controller handles it" — [MT variant.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h), [Meshtastic docs](https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/)
- Prebuilt C3 firmware files: `T-Keyboard_Keyboard_ESP32C3_241225.bin`, `_250620.bin`. Flash with `esptool --chip esp32c3 … write_flash -z --flash_mode dio --flash_freq 80m 0x0`. The 6-pin header order is TX, RX, BOOT, RST, GND, VCC — [LOCAL firmware/README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/firmware/README.MD)
- platformio.ini T-Keyboard env: `platform = espressif32@6.9.0`, `board = ttgo-t-oi-plus`, `board_build.flash_mode = dio` — [LOCAL platformio.ini](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/platformio.ini)

### Inferences
- **C3 firmware bug in the source:** `case LILYGO_KB_BRIGHTNESS_CMD` has no `break`, so it falls through into `case 0x02` and calls `Wire.read()` again. A 2-byte write `{0x01, duty}` therefore works (the extra read returns -1, which fails the `>30` check). A 3rd byte in the same transaction, however, would be taken as the Alt+B default duty. Send exactly 2 bytes.
- Hosts poll. The C3 holds only one pending char (`comdata`), so poll at least every ~10–20 ms or fast keypresses will be dropped.
- The shipped .bin may differ from this source. Raw mode (0x03) exists in the source; check that the installed C3 firmware version supports it.

### Gaps
- The full key → ASCII table (primary and symbol layers) is in `Keyboard_ESP32C3.ino` L110-205. It is not reproduced here; read it directly if you need the exact codes (e.g. how the Sym/Shift/mic/speaker keys behave).
- How GPIO46 (C3_INT) is driven by the C3 firmware is not visible in the source excerpts I read.

## Q6. GPS (T-Deck Plus built-in)

### Takeaway
The GPS is on Serial1, ESP RX = 44 and ESP TX = 43. **Two module variants exist**: a u-blox MIA-M10Q (schematic "T-Deck-GPS V1.0 24-03-29") and a Quectel L76K (schematic "T-Deck-L76K V1.0 24-08-12"). LilyGO first tries the L76K at 9600 baud. If that fails, it tries the u-blox M10 at 38400, then 115200, then 9600, using UBX commands. Firmware should autodetect both.

### Cited Findings
- `SerialGPS.begin(9600, SERIAL_8N1, BOARD_GPS_RX_PIN /*44*/, BOARD_GPS_TX_PIN /*43*/)`. L76K detection: send `$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n` (stop NMEA), then `$PCAS06,0*1B\r\n` (query version), and expect a reply starting with `$GPTXT,01,01,02`, then `gps_model = "L76K"` (3 retries) — [LOCAL UnitTest.ino L165-200](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- Fallback: `SerialGPS.updateBaudRate(38400)`, then try `GPS_Recovery()` at each of {38400, 115200, 9600}. On success: "UBlox-M10Q GNSS init succeeded", `gps_model = "UBlox-M10"`. Otherwise revert to 9600 — [LOCAL UnitTest.ino L1038-1060](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- `GPS_Recovery()` sends UBX-CFG-CFG clear frames `B5 62 06 09 0D 00 FF FF 00 00 00 00 00 00 00 00 00 00 02 1C A2`, `…01 1B A1`, `B5 62 06 09 0D 00 00 00 00 00 00 00 00 00 FF FF 00 00 03 1D B3` and then polls UBX-CFG-RATE `B5 62 06 08 00 00 0E 30`. It waits up to 800 ms for an ACK (class 0x05 id 0x01) or the response — [LOCAL UnitTest.ino L1225-1400](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- GPSShield example comment: "Set u-blox m10q gps baudrate 38400"; `SerialGPS.begin(38400, …)` — [LOCAL examples/GPSShield/GPSShield.ino L98-99](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/GPSShield/GPSShield.ino)
- Schematics: `T-Deck-GPS V1.0 24-03-29.pdf` names **MIA-M10Q** (with VRTC backup, IPEX GNSS antenna connector, 5-pin 2.0 mm connector PPS/GPS_TX/GPS_RX/VDD3V3/GND). `T-Deck-L76K V1.0 24-08-12.pdf` names **L76K** (RESET_N, WAKE_UP, 1PPS, the same 5-pin connector) — [LOCAL schematic/](https://github.com/Xinyuan-LilyGO/T-Deck/tree/master/schematic)
- Meshtastic: `GPS_DEFAULT_NOT_PRESENT 1`, `GPS_RX_PIN 44`, `GPS_TX_PIN 43`. Meshtastic's generic GPS probe autodetects the module and baud — [MT variant.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h)
- LilyGO wiki (search snippet only; the page returned 404 when fetched) lists the Plus GNSS as MIA-M10Q. The product page says only "GPS onboard". CNX describes GPS as "optional… connected to the Grove UART connector" — [LilyGO wiki (search result)](https://wiki.lilygo.cc/get_started/en/LCD_OLED/T-Deck-Plus/T-Deck-Plus.html), [product page](https://lilygo.cc/products/t-deck-plus-meshtastic), [CNX](https://cnx-software.com/?p=136193). **Conflict**: CNX calls GPS optional, while the README and product page say it is built in on the Plus, using the Grove pins.
- firmware/README: if "GPS positioning is inaccurate", use u-center2 with the GPSShield example or `T-Deck_UnitTest_Debug_251222.bin` — [LOCAL firmware/README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/firmware/README.MD)

### Inferences
- In practice: u-blox M10 units likely ship at 38400 (LilyGO pre-configured them, per the GPSShield comment), although the factory default of a bare MIA-M10Q is 9600 per u-blox convention. L76K units run at 9600. Autodetect order: 38400 → 9600 → 115200, and test both NMEA and UBX/PCAS replies.
- The later schematic date (24-08) suggests the L76K replaced the M10Q in later production, but there is no source for the cut-over date.
- The PPS line exists on the module connector, but neither the LilyGO nor the MT pin map gives it an ESP32 GPIO, so it is probably not routed to the S3.

### Gaps
- No source states which variant a specific 915 MHz internal-antenna unit (H737-A) ships with. Detect at runtime.
- Whether the GPS has its own power enable (separate from GPIO10) is undocumented.

## Q7. Audio (speaker I2S, ES7210 mic)

### Takeaway
The speaker is a MAX98357A on I2S0 (BCK 7, WS 5, DOUT 6). The microphones are dual MEMS through an ES7210 ADC at I2C 0x40, on I2S1 (MCLK 48, SCK 47, LRCK 21, DIN 14), sampled at 16 kHz.

### Cited Findings
- `MIC_I2S_SAMPLE_RATE 16000`, `MIC_I2S_PORT I2S_NUM_1`, `SPK_I2S_PORT I2S_NUM_0` — [LOCAL UnitTest.ino L49-51](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- `ES7210_ADDR = ES7210_AD1_AD0_00 = 0x40` — [LOCAL lib/es7210/src/es7210.h](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/lib/es7210/src/es7210.h)
- Schematic parts MAX98357A (net SD_MODE), ES7210 (nets ES7210_INT, ES7210_MCLK, ES7210_SCLK, ES7210_LRCK, ES7210_SDOUT) — [LOCAL schematic.pdf](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/schematic/schematic.pdf)
- CNX: "Speaker with MAX98357A amplifier and two MEMS microphones, controlled by an ES7210" — [CNX](https://cnx-software.com/?p=136193)

### Gaps
- The `I2SPlay`/`Microphone` example details (I2S modes, ES7210 gain registers) were not reviewed in depth.

## Q8. Power: battery, charging, sleep

### Takeaway
Battery voltage is on GPIO4 through a 1:2 divider (100k/100k): `analogReadMilliVolts(4) * 2`, or ×2.11 per Meshtastic's calibration. The charger is a TP4065B. Its CHRG net exists, but no source maps it to a GPIO. LilyGO's deep sleep wakes on GPIO0 (trackball press) via ext1 ALL_LOW and draws about 240 µA.

### Cited Findings
- `analogReadMilliVolts(BOARD_BAT_ADC) * 2` displayed as mV — [LOCAL examples/UnitTest/ui.cpp L457](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/ui.cpp)
- MT: "ratio of voltage divider = 2.0 (RD2=100k, RD3=100k)", `ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage`, `ADC_CHANNEL ADC_CHANNEL_3` — [MT variant.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h). **Conflict** with LilyGO's ×2.
- Charger TP4065B with net `CHRG` in the schematic; battery 2000 mAh; "battery voltage read on IO04" — [LOCAL schematic.pdf](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/schematic/schematic.pdf), [CNX](https://cnx-software.com/?p=136193), [product page](https://lilygo.cc/products/t-deck-plus-meshtastic)
- Sleep sequence: fade the backlight 16→1, `touch.sleep()`, `tft.writecommand(0x10)` (SLPIN), `SPI.end()`, `Wire.end()`, `esp_sleep_enable_ext1_wakeup(1ull << 0, ESP_EXT1_WAKEUP_ALL_LOW)`, `esp_deep_sleep_start()` — "Deep sleep consumes approximately 240uA of current". Optional: "If you need other peripherals to maintain power… `gpio_hold_en((gpio_num_t)BOARD_POWERON); gpio_deep_sleep_hold_en();`" (commented out) — [LOCAL UnitTest.ino L1080-1104](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- MT: `USE_POWERSAVE`, `SLEEP_TIME 120` — [MT variant.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h)
- The main power slide switch must be ON for flashing ("Toggle the switch to ON") — [LOCAL README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/README.MD)

### Inferences
- In deep sleep, GPIO10 is not held, so it floats LOW and peripherals power off (this gives the 240 µA figure). If you need LoRa RX wake-up (DIO1 = GPIO45 via ext1), you must hold GPIO10 HIGH, and current will rise sharply. GPIO45 is RTC-capable on the S3. Note that ext1 ALL_LOW suits GPIO0. DIO1 is active-high, so you would need ext0 level 1 or ext1 ANY_HIGH on S3.
- Since there is no charging GPIO, estimate charging from rising voltage or USB presence (USB VBUS detection is also not in any pin map).

### Gaps
- No source maps TP4065B CHRG/STDBY to an ESP32 GPIO. It probably drives an LED only. Check visually in the schematic.
- There is no battery %-vs-voltage curve from LilyGO.

## Q9. Build settings: core, libraries, flash/PSRAM, USB-CDC, partitions

### Takeaway
PlatformIO `platform = espressif32@6.3.0` (Arduino-ESP32 2.0.9). Board JSON: 16 MB QIO flash at 80 MHz, `memory_type qio_opi` (8 MB OPI PSRAM), `default_16MB.csv`, `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`. Bundled libs: LVGL 8.4.0, RadioLib 7.1.0, TFT_eSPI 2.5.43 (patched Setup210), SensorLib 0.2.0, TinyGPSPlus 1.0.3, ESP32-audioI2S 2.0.6, AceButton 1.3.3, Arduino_GFX 1.3.7, TouchLib 0.0.2. For Arduino IDE, LilyGO says to use core 2.0.14 or lower.

### Cited Findings
- `[env:T-Deck] platform = espressif32@6.3.0, board = T-Deck, framework = arduino, upload_speed = 921600, monitor_speed = 115200`, build_flags `-DBOARD_HAS_PSRAM=1 -DCORE_DEBUG_LEVEL=1 -DARDUINO_USB_CDC_ON_BOOT=1 -DDISABLE_ALL_LIBRARY_WARNINGS` plus many `-DRADIOLIB_EXCLUDE_*`. The comment explains that CDC_ON_BOOT "will start printing and wait for terminal access during startup"; to avoid blocking on battery, use `-UARDUINO_USB_CDC_ON_BOOT` — [LOCAL platformio.ini](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/platformio.ini)
- `boards/T-Deck.json`: `ldscript esp32s3_out.ld`, `partitions default_16MB.csv`, `memory_type qio_opi`, extra_flags `-DARDUINO_USB_MODE=1 -DARDUINO_RUNNING_CORE=1 -DARDUINO_EVENT_RUNNING_CORE=1`, `f_cpu 240000000L`, `f_flash 80000000L`, `flash_mode qio`, hwids 0x303A/0x1001, `flash_size 16MB`, `maximum_ram_size 327680`, `maximum_size 16777216`, name "LilyGo T-Decl(16M Flash 8M OPI PSRAM )" [sic] — [LOCAL boards/T-Deck.json](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/boards/T-Deck.json)
- UnitTest refuses to build without PSRAM: `#error "Detected that PSRAM is not turned on. Please set PSRAM to OPI PSRAM in ArduinoIDE"` — [LOCAL UnitTest.ino L38-40](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- It also refuses to build against a non-patched TFT_eSPI (checks TFT_DC/CS/MOSI/SCLK against the board pins) — [LOCAL UnitTest.ino L56-60](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- Library versions (from library.properties/library.json in LOCAL lib/): lvgl 8.4.0 (`LVGL_VERSION_MAJOR 8, MINOR 4, PATCH 0`), RadioLib 7.1.0, TFT_eSPI 2.5.43, SensorsLib 0.2.0, TinyGPSPlus 1.0.3, ESP32-audioI2S 2.0.6, AceButton 1.3.3, Arduino_GFX 1.3.7, TouchLib 0.0.2, es7210 1.0. `lib/lv_conf.h` is provided — [LOCAL lib/](https://github.com/Xinyuan-LilyGO/T-Deck/tree/master/lib)
- README Arduino IDE: "The test phase is using 2.0.14… When the operation is abnormal, please downgrade to a version below 2.0.14", "TFT_eSPI does not work on versions higher than 2.0.14" — [LOCAL README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/README.MD)
- The repo README now points to LilyGoLib for the new factory firmware: "For the factory firmware of the new version, please visit LilyGoLib" — [LOCAL README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/README.MD)
- The TouchFix bin was built with framework-arduinoespressif32@3.20009.0 / IDF v4.4.4 (coordinator's binary string analysis, not verified by me). This matches espressif32@6.3.0.
- MT board `t-deck.json`: same QIO/80 MHz/qio_opi/16 MB, extra flags `-DBOARD_HAS_PSRAM -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1 -DARDUINO_RUNNING_CORE=1 -DARDUINO_EVENT_RUNNING_CORE=1`, `use_1200bps_touch: true`, `wait_for_upload_port: true`, variant "t-deck"; MT env `board_build.partitions = default_16MB.csv`, `custom_meshtastic_hw_model = 50` (T_DECK), `requires_dfu = true`. The t-deck-tft env uses LovyanGFX 1.2.28, `-D CONFIG_DISABLE_HAL_LOCKS=1`, `RAM_SIZE=5120`, `LGFX_BUFSIZE=153600`, `VIEW_320x240`, `HAS_SDCARD`, `USE_I2S_BUZZER` — [MT boards/t-deck.json](https://github.com/meshtastic/firmware/blob/develop/boards/t-deck.json), [MT platformio.ini](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/platformio.ini)
- The repo ships prebuilt Meshtastic images `firmware-t-deck-tft-2.7.10.94d4bdf.bin` and `firmware-t-deck-pro-2.7.10.94d4bdf.bin` — [LOCAL firmware/](https://github.com/Xinyuan-LilyGO/T-Deck/tree/master/firmware)
- Flashing a merged image: `esptool --chip esp32s3 --baud 921600 … write_flash -z --flash_mode dio --flash_freq 80m 0x0 <bin>` — [LOCAL firmware/README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/firmware/README.MD)

### Inferences
- Use espressif32@6.3.0 (or any 2.0.x core ≤ 2.0.14) if you use LilyGO's TFT_eSPI. With Arduino-ESP32 3.x, use LovyanGFX instead of TFT_eSPI, as Meshtastic does.
- `memory_type qio_opi` (QIO flash + OPI PSRAM) is required. The wrong PSRAM mode causes boot loops or no PSRAM.
- With CDC_ON_BOOT=1, `Serial` goes over native USB. Do not block on `while(!Serial)` when running on battery.

### Gaps
- The repo pins only the PlatformIO platform (6.3.0 = Arduino core 2.0.9), not an Arduino IDE core version; the README recommends 2.0.14 for the IDE. **Minor conflict**: PIO uses 2.0.9, the IDE guidance says 2.0.14.

## Q10. Known gotchas and bring-up order

### Takeaway
Bring-up order: GPIO10 HIGH (immediately, plus about 100 ms) → all SPI CS (9, 12, 39) as OUTPUT HIGH → MISO INPUT_PULLUP → `SPI.begin(40, 38, 41)` → display → `Wire.begin(18, 8)` → GT911 (0x5D/0x14) → keyboard 0x55 → backlight → SD (CS 39) → radio → GPS.

### Cited Findings
- UnitTest `setup()` order: `Serial.begin(115200)`; `pinMode(10, OUTPUT); digitalWrite(10, HIGH)`; comment "Set CS on all SPI buses to high level during initialization": CS 39/9/12 OUTPUT HIGH; `pinMode(BOARD_SPI_MISO, INPUT_PULLUP)`; `SPI.begin(40, 38, 41)`; GPIO0/1/2/3/15 INPUT_PULLUP; `tft.begin()`; `setRotation(1)`; `Wire.begin(18, 8)`; touch; keyboard check; backlight `setBrightness(16)`; LVGL; SPIFFS; SD; radio; codec; I2S amp; mic; GPS; UI; WiFi — [LOCAL UnitTest.ino L858-1063](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- SD: all CS HIGH again before `SD.begin(BOARD_SDCARD_CS, SPI, 800000U)` (800 kHz). Before every radio operation in the loop, CS 39/9/12 are set HIGH again — [LOCAL UnitTest.ino L398-404, L520-525](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino). MT uses `SD_SPI_FREQUENCY 75000000U` — [MT variant.h](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.h). **Large difference** (800 kHz vs 75 MHz).
- The UnitTest uses a FreeRTOS binary semaphore (`xSemaphore`) around TFT flushes, "to allow multitasking access" to the shared bus — [LOCAL UnitTest.ino](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/examples/UnitTest/UnitTest.ino)
- MT `earlyInitVariant()`: GPIO10 HIGH, then CS 9/39/12 HIGH, then `delay(100)`. Comment: "We need to initialize all CS pins in advance otherwise there will be SPI communication issues e.g. when detecting the SD card" — [MT variant.cpp](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/t-deck/variant.cpp)
- Download mode: hold the trackball centre (GPIO0), press and release RST, then release the trackball. With Meshtastic installed, the USB port is virtual, so manual download mode may be needed. Issue #62 mentions a resistor that can prevent upload — [LOCAL README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/README.MD), [LOCAL firmware/README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/firmware/README.MD). Meshtastic: in download mode "the screen should be black with backlight off" — [Meshtastic docs](https://meshtastic.org/docs/hardware/devices/lilygo/tdeck/)
- `esp_vad.h` missing error: see issue #90 — [LOCAL README.MD](https://github.com/Xinyuan-LilyGO/T-Deck/blob/master/README.MD)

### Inferences
- If the radio is set up through RadioLib's default `SPI` object, it shares the bus with TFT_eSPI. If you also use LVGL/TFT DMA on another task, guard the bus with a single mutex. Meshtastic sets `CONFIG_DISABLE_HAL_LOCKS=1` for stability in its TFT build.
- I2C runs at 100 kHz in LilyGO (log `freq=100000`) and 400 kHz in MT's GT911 config. The C3 slave is set to 100 kHz, so 100 kHz is the safest shared-bus setting.

### Gaps
- No source covers the Plus internal-antenna SKU's RF performance or SAR/regulatory limits for 22 dBm into the internal antenna.
- The LilyGO wiki page for the T-Deck Plus returned 404 on both URLs tried, so the official spec table could only be seen through search snippets.
