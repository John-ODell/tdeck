#include "console.h"
#include <Arduino.h>
#include <lvgl.h>
#include <mbedtls/base64.h>
#include "hal/input.h"
#include "ui/ui.h"
#include "mesh/mesh.h"
#include "core/storage.h"
#include <SD.h>
#include <LittleFS.h>

static void shot()
{
    // Snapshot of the active screen (the top layer with toasts isn't included)
    lv_img_dsc_t *img = lv_snapshot_take(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    if (!img) { Serial.println("SHOT failed"); return; }
    int w = img->header.w, h = img->header.h;
    Serial.printf("SHOT %d %d\n", w, h);
    uint16_t *px = (uint16_t *)img->data;
    static uint8_t raw[480];
    static unsigned char enc[660];
    for (int i = 0; i < w * h; i += 240) {
        int n = w * h - i < 240 ? w * h - i : 240;
        for (int k = 0; k < n; k++) {
            uint16_t v = px[i + k];
#if LV_COLOR_16_SWAP
            v = (uint16_t)(v << 8 | v >> 8);
#endif
            raw[2 * k] = v & 0xff;
            raw[2 * k + 1] = v >> 8;
        }
        size_t olen = 0;
        mbedtls_base64_encode(enc, sizeof(enc), &olen, raw, 2 * n);
        Serial.write(enc, olen);
        Serial.write('\n');
    }
    Serial.println("END");
    lv_snapshot_free(img);
}

// "flash:/x" forces internal flash; otherwise the SD card when mounted, else flash,
// which is the rule the apps use for their files.
static fs::FS &pick(const char *&path)
{
    if (!strncmp(path, "flash:", 6)) { path += 6; return LittleFS; }
    return sd_ok() ? (fs::FS &)SD : (fs::FS &)LittleFS;
}

static void ls(const char *path)
{
    fs::FS &fs = pick(path);
    File d = fs.open(*path ? path : "/");
    if (!d || !d.isDirectory()) { Serial.println("LS no such directory"); return; }
    Serial.printf("LS %s (%s)\n", path, &fs == (fs::FS *)&LittleFS ? "flash" : "sd");
    while (File f = d.openNextFile()) {
        Serial.printf("  %s%s  %lu\n", f.name(), f.isDirectory() ? "/" : "", (unsigned long)f.size());
        f.close();
    }
    d.close();
    Serial.println("END");
}

static void cat(const char *path)
{
    fs::FS &fs = pick(path);
    File f = fs.open(path, "r");
    if (!f || f.isDirectory()) { Serial.println("CAT no such file"); return; }
    Serial.printf("CAT %s %lu\n", path, (unsigned long)f.size());
    while (f.available()) Serial.write(f.read());
    f.close();
    Serial.println("\nEND");
}

static uint32_t parse_key(const char *k)
{
    if (!strcasecmp(k, "enter")) return LV_KEY_ENTER;
    if (!strcasecmp(k, "esc")) return LV_KEY_ESC;
    if (!strcasecmp(k, "up")) return LV_KEY_PREV;
    if (!strcasecmp(k, "down")) return LV_KEY_NEXT;
    if (!strcasecmp(k, "left")) return LV_KEY_LEFT;
    if (!strcasecmp(k, "right")) return LV_KEY_RIGHT;
    return strlen(k) == 1 ? (uint8_t)k[0] : 0;
}

void console_poll()
{
    static char line[64];
    static size_t len = 0;
    while (Serial.available()) {
        int c = Serial.read();
        if (c != '\n' && c != '\r') {
            if (len < sizeof(line) - 1) line[len++] = (char)c;
            continue;
        }
        if (!len) continue;
        line[len] = 0;
        len = 0;
        if (!strncmp(line, "open ", 5)) Serial.println(ui_open_app_named(line + 5) ? "ok" : "no such app");
        else if (!strncmp(line, "key ", 4)) {
            uint32_t k = parse_key(line + 4);
            if (k) input_inject_key(k);
            Serial.println(k ? "ok" : "bad key");
        } else if (!strcmp(line, "home")) { ui_go_home(); Serial.println("ok"); }
        else if (!strcmp(line, "shot")) shot();
        else if (!strcmp(line, "lock")) { screen_lock(); Serial.println("ok"); }
        else if (!strcmp(line, "unlock")) { screen_unlock(); Serial.println("ok"); }
        else if (!strncmp(line, "ls ", 3)) ls(line + 3);
        else if (!strncmp(line, "cat ", 4)) cat(line + 4);
        else if (!strcmp(line, "rssi")) mesh::debug_rssi();
        else Serial.println("commands: open <app> | key <k> | home | shot | lock | unlock | ls <dir> | cat <file>");
    }
}
