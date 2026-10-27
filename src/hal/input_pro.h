// T-Deck Pro extras on top of input.h, for the bring-up program and a future status bar.
#pragma once
#include <stdint.h>
#include "hal/keymap_pro.h"
#include "hal/touch_cst66xx.h"

struct ProInputStats {
    uint32_t keys_delivered;     // keys queued for LVGL, repeats included
    uint32_t keys_dropped;       // queue full (should stay 0)
    uint32_t kb_events, kb_overflows, kb_edges;
    uint32_t touch_reports, touch_checksum_errors, touch_i2c_errors, touch_edges;
    uint32_t max_kb_wait_ms;     // longest time from INT asserted to the FIFO being drained
};
ProInputStats input_pro_stats();
const cst::Info &input_pro_touch_info();
const kb::State &input_pro_mods();
// Called for every key delivered, after the keymap (bring-up logging). Not a filter.
typedef void (*input_pro_key_log_t)(uint8_t code, uint32_t key);
void input_pro_set_key_log(input_pro_key_log_t log);
