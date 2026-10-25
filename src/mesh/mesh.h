// Meshtastic-compatible mesh node: radio, router (flooding relay, dedupe,
// ACKs, PKI DMs) and the NodeInfo / Position / Telemetry / Text ports.
#pragma once
#include <stdint.h>
#include "core/messages.h"

namespace mesh {

struct Stats {
    uint32_t rx_ok, rx_bad, rx_dupe, tx, relayed;
    float last_rssi, last_snr;
    float freq_mhz;
    uint8_t channel_hash;
};

bool init();
void loop();
void apply_settings();               // re-tune radio after settings change
bool radio_ok();

// Band sweep for the spectrum app. Mesh RX/TX pause between begin and end;
// queued packets go out once end() re-tunes the radio to the channel.
bool sweep_begin(float bw_khz);
float sweep_rssi(float mhz, uint32_t settle_us = 1000); // instantaneous RSSI (dBm); <1 ms reads invalid (-127.5)
void debug_rssi();                  // console: RSSI probes at several settle times
void sweep_end();
bool sweeping();
void flush_storage();                // write node DB and messages now (before sleep)
uint32_t our_num();
const Stats &stats();

uint32_t send_text(uint32_t to, const char *text);   // 0 on failure
void send_nodeinfo(uint32_t to, bool want_response);
void send_position(uint32_t to = 0xFFFFFFFF);
void send_telemetry();

// Fed by the GPS module
void set_own_position(int32_t lat_i, int32_t lon_i, int32_t alt, uint8_t sats);
bool own_position(int32_t *lat_i, int32_t *lon_i);

typedef void (*text_cb_t)(const Message &m);
void set_text_callback(text_cb_t cb);

} // namespace mesh
