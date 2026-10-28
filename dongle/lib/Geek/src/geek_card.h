// The GEEK's TF card as a USB drive, shared with the firmware.
//
// The card belongs to the computer except while the firmware has taken it: then the
// computer sees it removed, the firmware mounts it (SD_MMC, at "/sd") and writes, and
// give() hands it back by re-plugging the whole USB device, since flagging the medium
// as changed is not enough for macOS to re-read it. Reads can happen while the computer
// has the drive too; reading() remounts first if the computer wrote since.
// Needs -DGEEK_SD. All of this is before/after USB.begin() as noted.
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

// Before USB.begin(): mount the card and add it to the composite device as a drive with
// these SCSI vendor and product strings (8 and 16 characters at most). False without a card.
bool geek_card_init(const char *vendor, const char *product, const char *revision = "1.0");

bool geek_card_ok();        // a card was mounted at init
bool geek_card_presented(); // the computer has the drive right now
uint64_t geek_card_size();  // bytes

// Take the card from the computer and remount it here; true if it is ours (also when it
// already was). Write through SD_MMC, then give() it back.
bool geek_card_take();
void geek_card_give();

// For a read while the computer keeps the drive: remounts if the computer wrote to it.
// False without a card or if the remount failed.
bool geek_card_reading();

// "a/b/c.txt" relative to the card root -> "/a/b/c.txt"; refuses "..", makes the folders
// on the way if asked. The result is an SD_MMC path (open it with SD_MMC.open()).
bool geek_card_path(const char *rel, char *out, size_t cap, bool make_dirs);
