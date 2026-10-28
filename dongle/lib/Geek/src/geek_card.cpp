#ifdef GEEK_SD
#include "geek_card.h"
#include "geek_pins.h"
#include <USBMSC.h>
#include <sdmmc_cmd.h>
#include "tusb.h"

static USBMSC s_msc;
static bool s_sd = false, s_presented = false;
static volatile bool s_host_wrote = false; // the computer wrote since we last looked

struct SDAccess : public fs::SDMMCFS {
    sdmmc_card_t *card() { return _card; }
};
static sdmmc_card_t *sd_card() { return static_cast<SDAccess &>(SD_MMC).card(); }

static int32_t msc_read(uint32_t lba, uint32_t off, void *buf, uint32_t n)
{
    sdmmc_card_t *c = sd_card();
    if (!c || off || !s_presented) return -1;
    return sdmmc_read_sectors(c, buf, lba, n / 512) == ESP_OK ? (int32_t)n : -1;
}

static int32_t msc_write(uint32_t lba, uint32_t off, uint8_t *buf, uint32_t n)
{
    sdmmc_card_t *c = sd_card();
    if (!c || off || !s_presented) return -1;
    s_host_wrote = true;
    return sdmmc_write_sectors(c, buf, lba, n / 512) == ESP_OK ? (int32_t)n : -1;
}

static bool sd_begin()
{
    SD_MMC.setPins(GEEK_SD_CLK, GEEK_SD_CMD, GEEK_SD_D0, GEEK_SD_D1, GEEK_SD_D2, GEEK_SD_D3);
    s_sd = SD_MMC.begin("/sd", false, false, 20000);
    return s_sd;
}

bool geek_card_init(const char *vendor, const char *product, const char *revision)
{
    if (!sd_begin()) return false;
    s_msc.vendorID(vendor);
    s_msc.productID(product);
    s_msc.productRevision(revision);
    s_msc.onRead(msc_read);
    s_msc.onWrite(msc_write);
    s_msc.mediaPresent(true);
    s_presented = true;
    s_msc.begin(SD_MMC.cardSize() / 512, 512);
    return true;
}

bool geek_card_ok() { return s_sd; }
bool geek_card_presented() { return s_presented; }
uint64_t geek_card_size() { return s_sd ? SD_MMC.cardSize() : 0; }

bool geek_card_take()
{
    if (!s_sd) return false;
    if (!s_presented) return true;
    s_msc.mediaPresent(false);
    s_presented = false;
    delay(50);
    SD_MMC.end(); // drop anything cached from before the computer wrote to it
    return sd_begin();
}

void geek_card_give()
{
    if (!s_sd || s_presented) return;
    s_msc.mediaPresent(true);
    s_presented = true;
    tud_disconnect();
    delay(300);
    tud_connect();
}

bool geek_card_reading()
{
    if (!s_sd) return false;
    if (s_presented && s_host_wrote) {
        s_host_wrote = false;
        s_msc.mediaPresent(false);
        delay(20);
        SD_MMC.end();
        bool ok = sd_begin();
        s_msc.mediaPresent(true);
        if (!ok) return false;
    }
    return true;
}

bool geek_card_path(const char *rel, char *out, size_t cap, bool make_dirs)
{
    if (strstr(rel, "..")) return false;
    while (*rel == '/') rel++;
    snprintf(out, cap, "/%s", rel);
    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/') out[--n] = 0;
    if (make_dirs)
        for (size_t i = 1; i < n; i++)
            if (out[i] == '/') { out[i] = 0; SD_MMC.mkdir(out); out[i] = '/'; }
    return true;
}
#endif
