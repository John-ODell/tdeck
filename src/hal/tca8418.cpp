#include "hal/tca8418.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"

namespace tca {

enum : uint8_t {
    REG_CFG = 0x01, REG_INT_STAT = 0x02, REG_KEY_LCK_EC = 0x03, REG_KEY_EVENT_A = 0x04,
    REG_GPIO_INT_EN1 = 0x1A, REG_KP_GPIO1 = 0x1D, REG_GPI_EM1 = 0x20, REG_GPIO_DIR1 = 0x23,
};
// CFG: AI (auto-increment) | OVR_FLOW_IEN | KE_IEN. INT stays low until INT_STAT is cleared.
static const uint8_t CFG = 0x80 | 0x08 | 0x01;
static const uint8_t INT_K = 0x01, INT_OVR_FLOW = 0x08;

static volatile uint32_t s_edges;
static volatile bool s_edge;
static Stats s_stats;

static void IRAM_ATTR on_int() { s_edges++; s_edge = true; }

static bool write_reg(uint8_t reg, uint8_t v)
{
    Wire.beginTransmission(KEYBOARD_I2C_ADDR);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

int read_reg(uint8_t reg)
{
    Wire.beginTransmission(KEYBOARD_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((int)KEYBOARD_I2C_ADDR, 1) != 1) return -1;
    return Wire.read();
}

bool init()
{
    if (read_reg(REG_CFG) < 0) return false;
    // Rows 0-3 and columns 0-9 form the matrix. The other pins are plain inputs that make
    // no events or interrupts (the factory firmware enabled both on floating pins).
    for (int i = 0; i < 3; i++) {
        write_reg(REG_GPIO_DIR1 + i, 0x00);
        write_reg(REG_GPI_EM1 + i, 0x00);
        write_reg(REG_GPIO_INT_EN1 + i, 0x00);
    }
    write_reg(REG_KP_GPIO1, 0x0F);
    write_reg(REG_KP_GPIO1 + 1, 0xFF);
    write_reg(REG_KP_GPIO1 + 2, 0x03);
    // Drop anything queued before boot, clear the flags, then enable the interrupt
    for (int i = 0; i < 16 && (read_reg(REG_KEY_LCK_EC) & 0x0F); i++) read_reg(REG_KEY_EVENT_A);
    write_reg(REG_INT_STAT, 0x1F);
    write_reg(REG_CFG, CFG);
    pinMode(BOARD_KEYBOARD_INT, INPUT_PULLUP);
    attachInterrupt(BOARD_KEYBOARD_INT, on_int, FALLING);
    return read_reg(REG_CFG) == CFG;
}

bool pending() { return s_edge || digitalRead(BOARD_KEYBOARD_INT) == LOW; }

int drain(event_cb on_event)
{
    s_edge = false;
    int n = 0;
    for (int pass = 0; pass < 4; pass++) {
        int count = read_reg(REG_KEY_LCK_EC);
        if (count < 0) break;
        count &= 0x0F;
        for (int i = 0; i < count; i++) {
            int ev = read_reg(REG_KEY_EVENT_A);  // each read pops one event
            if (ev <= 0) break;
            n++;
            if (on_event) on_event(ev & 0x7F, (ev & 0x80) != 0);
        }
        int st = read_reg(REG_INT_STAT);
        if (st > 0) {
            if (st & INT_OVR_FLOW) s_stats.overflows++;
            write_reg(REG_INT_STAT, st & (INT_K | INT_OVR_FLOW));  // write 1 to clear
        }
        if (count == 0) break;
    }
    s_stats.events += n;
    return n;
}

Stats stats()
{
    Stats s = s_stats;
    s.edges = s_edges;
    return s;
}

}  // namespace tca
