// Music: MP3 and WAV from /music on the SD card through the speaker. Every folder in
// /music is a playlist named after it; loose files are the "Music" playlist, as on the
// Pi. Playback is decoded a frame at a time from the main loop (music_loop), so it keeps
// going with the app closed, and the DMA holds enough sound to ride out a screen redraw.
// `s` pulls whatever is under music/ on the GEEK's card that the T-Deck doesn't have yet.
#include "apps.h"
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include <vector>
#include <algorithm>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutput.h>
#include <AudioLogger.h>
#include "core/storage.h"
#include "hal/audio.h"
#include "link/companion.h"
#include "link/link.h"
#include "ui/theme.h"

static const char *ROOT = "/music";

// ---- library ---------------------------------------------------------------------------

struct Playlist { String name, dir; std::vector<String> files; };
static std::vector<Playlist> s_lists;

static bool is_audio(const String &n)
{
    String l = n;
    l.toLowerCase();
    return l.endsWith(".mp3") || l.endsWith(".wav");
}

static void scan_dir(const String &dir, std::vector<String> &out, std::vector<String> *subdirs)
{
    File d = SD.open(dir);
    if (!d) return;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
        String n = f.name();
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        if (n[0] == '.') { f.close(); continue; }
        if (f.isDirectory()) { if (subdirs) subdirs->push_back(n); }
        else if (is_audio(n)) out.push_back(n);
        f.close();
        if (out.size() >= 500) break;
    }
    d.close();
    std::sort(out.begin(), out.end());
}

static int s_list = -1, s_index = -1;

static void scan()
{
    // The lists are rebuilt from the card, so the current track is found again by name
    // afterwards; a stale index into the old lists made the first click after a sync a
    // silent no-op (#72)
    String cur_dir, cur_file;
    if (s_list >= 0 && s_index >= 0) { cur_dir = s_lists[s_list].dir; cur_file = s_lists[s_list].files[s_index]; }
    s_list = s_index = -1;
    s_lists.clear();
    if (!sd_mount()) { Serial.println("[music] scan: no card"); return; }
    if (!SD.exists(ROOT)) SD.mkdir(ROOT);
    std::vector<String> subs;
    Playlist loose = {"Music", String(ROOT), {}};
    scan_dir(ROOT, loose.files, &subs);
    std::sort(subs.begin(), subs.end());
    for (auto &s : subs) {
        Playlist p = {s, String(ROOT) + "/" + s, {}};
        scan_dir(p.dir, p.files, nullptr);
        if (!p.files.empty()) s_lists.push_back(p);
    }
    if (!loose.files.empty()) s_lists.push_back(loose);
    for (size_t l = 0; l < s_lists.size() && s_list < 0; l++)
        if (s_lists[l].dir == cur_dir)
            for (size_t i = 0; i < s_lists[l].files.size(); i++)
                if (s_lists[l].files[i] == cur_file) { s_list = l; s_index = i; break; }
}

// "03-Quiet_Engine.mp3" shows as "Quiet Engine" until the tag says otherwise
static String pretty(const String &file)
{
    String t = file;
    int dot = t.lastIndexOf('.');
    if (dot > 0) t = t.substring(0, dot);
    int i = 0;
    while (i < (int)t.length() && (isdigit(t[i]) || t[i] == '-' || t[i] == '_' || t[i] == ' ')) i++;
    if (i < (int)t.length()) t = t.substring(i);
    t.replace('_', ' ');
    return t;
}

// ---- player ----------------------------------------------------------------------------

// Samples go to the shared amplifier driver; when its DMA is full the decoder waits a loop
class AmpOutput : public AudioOutput {
public:
    bool begin() override { return audio_open(hertz ? hertz : 44100); }
    bool SetRate(int hz) override { hertz = hz; return audio_open(hz); }
    bool ConsumeSample(int16_t sample[2]) override
    {
        if (!drain()) return false;
        MakeSampleStereo16(sample);
        buf[2 * fill] = Amplify(sample[LEFTCHANNEL]);
        buf[2 * fill + 1] = Amplify(sample[RIGHTCHANNEL]);
        fill++;
        frames++;
        if (fill == N) drain();
        return true;
    }
    bool stop() override { fill = sent = 0; audio_silence(); return true; }
    uint32_t frames = 0;        // consumed since begin(), for the elapsed time
    uint32_t rate() const { return hertz ? hertz : 44100; }
private:
    static const int N = 256;
    int16_t buf[N * 2];
    int fill = 0, sent = 0;
    bool drain()
    {
        if (fill < N) return true;
        sent += audio_write(buf + 2 * sent, fill - sent);
        if (sent < fill) return false;
        fill = sent = 0;
        return true;
    }
};

static AmpOutput *s_out = nullptr;
static AudioGenerator *s_gen = nullptr;
static AudioFileSourceSD *s_file = nullptr;
static AudioFileSourceID3 *s_id3 = nullptr;
static bool s_playing = false, s_paused = false;
static int s_volume = 60;
static String s_title, s_artist;
static char s_error[48] = "";

static void apply_volume()
{
    if (s_out) s_out->SetGain(s_volume / 100.0f * 1.5f);
}

static void stop_track()
{
    if (s_gen) { s_gen->stop(); delete s_gen; s_gen = nullptr; }
    if (s_id3) { delete s_id3; s_id3 = nullptr; }
    if (s_file) { s_file->close(); delete s_file; s_file = nullptr; }
    if (s_out) { s_out->stop(); delete s_out; s_out = nullptr; }
    s_playing = s_paused = false;
}

static void on_tag(void *, const char *type, bool, const char *str)
{
    if (!strcmp(type, "Title")) s_title = str;
    else if (!strcmp(type, "Artist")) s_artist = str;
}

static bool play_track(int list, int index)
{
    stop_track();
    if (list < 0 || list >= (int)s_lists.size() || index < 0 || index >= (int)s_lists[list].files.size()) {
        Serial.printf("[music] play %d/%d: no such track (%d lists)\n", list, index, (int)s_lists.size());
        strlcpy(s_error, "track not found", sizeof(s_error));
        return false;
    }
    Playlist &p = s_lists[list];
    s_list = list;
    s_index = index;
    s_title = pretty(p.files[index]);
    s_artist = "";
    s_error[0] = 0;
    String path = p.dir + "/" + p.files[index];
    audioLogger = &Serial;
    s_file = new AudioFileSourceSD(path.c_str());
    Serial.printf("[music] play %s open=%d size=%lu\n", path.c_str(), s_file->isOpen(), (unsigned long)s_file->getSize());
    if (!s_file->isOpen()) { Serial.println("[music] can't open file"); strlcpy(s_error, "can't open file", sizeof(s_error)); stop_track(); return false; }
    s_out = new AmpOutput();
    apply_volume();
    String l = p.files[index];
    l.toLowerCase();
    AudioFileSource *src = s_file;
    if (l.endsWith(".mp3")) {
        s_id3 = new AudioFileSourceID3(s_file);
        s_id3->RegisterMetadataCB(on_tag, nullptr);
        src = s_id3;
        s_gen = new AudioGeneratorMP3();
    } else {
        s_gen = new AudioGeneratorWAV();
    }
    if (!s_gen->begin(src, s_out)) { Serial.println("[music] can't decode"); strlcpy(s_error, "can't decode", sizeof(s_error)); stop_track(); return false; }
    Serial.println("[music] decoding");
    s_playing = true;
    return true;
}

static void next_track(int step)
{
    if (s_list < 0) return;
    int n = s_lists[s_list].files.size();
    if (!n) return;
    play_track(s_list, ((s_index + step) % n + n) % n);
}

// From the main loop: decode while the amplifier has room, a few ms at most
void music_loop()
{
    if (!s_playing || s_paused || !s_gen) return;
    uint32_t until = millis() + 6;
    while (s_gen->isRunning() && millis() < until)
        if (!s_gen->loop()) break;
    if (!s_gen->isRunning()) {
        Serial.printf("[music] track ended at %lu frames\n", (unsigned long)(s_out ? s_out->frames : 0));
        // Track over (or unreadable): on to the next one
        int list = s_list, index = s_index;
        stop_track();
        int n = s_lists[list].files.size();
        if (n) play_track(list, (index + 1) % n);
    }
}

static void toggle()
{
    if (s_playing) {
        s_paused = !s_paused;
        if (s_paused) audio_silence();
    } else if (s_list >= 0 && s_index >= 0) {
        play_track(s_list, s_index);
    } else if (!s_lists.empty()) {
        play_track(0, 0);
    } else {
        Serial.printf("[music] toggle: nothing to play (card %d)\n", (int)sd_ok());
        strlcpy(s_error, sd_ok() ? "no music found" : "no SD card", sizeof(s_error));
    }
}

// ---- sync from the GEEK's card ---------------------------------------------------------
// Lists music/ and every folder in it, then pulls each file the SD doesn't have.

struct Pull { String remote, local; };
static std::vector<String> s_sync_dirs;
static std::vector<Pull> s_sync_pulls;
static int s_sync_got = 0;
static bool s_syncing = false;
static char s_sync_status[40] = "";

static void sync_step();

static void sync_finish(const char *msg)
{
    s_syncing = false;
    snprintf(s_sync_status, sizeof(s_sync_status), "%s", msg);
    ui_toast(msg);
    scan();
}

static void pulled(bool ok)
{
    if (ok) s_sync_got++;
    else { char m[64]; snprintf(m, sizeof(m), "pull failed: %s", companion::status()); sync_finish(m); return; }
    sync_step();
}

static void listed(bool ok, const companion::FsEntry *e, size_t n)
{
    if (!ok) { sync_finish("no music/ folder on the card"); return; }
    String dir = s_sync_dirs.front();
    s_sync_dirs.erase(s_sync_dirs.begin());
    bool root = dir == "music";
    for (size_t i = 0; i < n; i++) {
        String name = e[i].name;
        if (e[i].dir) { if (root) s_sync_dirs.push_back(dir + "/" + name); continue; }
        if (!is_audio(name)) continue;
        String local = String(ROOT) + dir.substring(5) + "/" + name;
        if (!SD.exists(local)) s_sync_pulls.push_back({dir + "/" + name, local});
    }
    sync_step();
}

static void sync_step()
{
    if (!s_sync_dirs.empty()) {
        snprintf(s_sync_status, sizeof(s_sync_status), "listing %s", s_sync_dirs.front().c_str());
        if (!companion::fs_list(s_sync_dirs.front().c_str(), listed)) sync_finish("dongle is busy");
        return;
    }
    if (!s_sync_pulls.empty()) {
        Pull p = s_sync_pulls.front();
        s_sync_pulls.erase(s_sync_pulls.begin());
        snprintf(s_sync_status, sizeof(s_sync_status), "pulling %d left", (int)s_sync_pulls.size() + 1);
        if (!companion::fs_get(p.remote.c_str(), p.local.c_str(), pulled)) sync_finish("dongle is busy");
        return;
    }
    char m[48];
    snprintf(m, sizeof(m), s_sync_got ? "synced %d new files" : "nothing new on the card", s_sync_got);
    sync_finish(m);
}

static void sync_start()
{
    if (!sd_ok()) { ui_toast("Music needs an SD card"); return; }
    if (!dongle::online()) { ui_toast("Dongle is offline"); return; }
    if (s_syncing || companion::busy()) { ui_toast("Already syncing"); return; }
    s_syncing = true;
    s_sync_got = 0;
    s_sync_dirs = {"music"};
    s_sync_pulls.clear();
    sync_step();
}

// ---- the screen ------------------------------------------------------------------------

enum View { NOW, LISTS, TRACKS };
static View s_view = NOW;
static int s_browse = 0; // playlist shown in TRACKS
static lv_obj_t *s_root = nullptr, *s_big, *s_sub, *s_time, *s_hint;

static void show_now();
static void show_lists();
static void show_tracks(int list);

static void refresh_now()
{
    if (!s_root || s_view != NOW) return;
    if (s_list >= 0 && s_index >= 0) {
        lv_label_set_text(s_big, s_title.c_str());
        Playlist &p = s_lists[s_list];
        lv_label_set_text_fmt(s_sub, "%s%s%s  %d/%d", s_artist.c_str(), s_artist.length() ? "  -  " : "", p.name.c_str(), s_index + 1, (int)p.files.size());
    } else {
        lv_label_set_text(s_big, s_lists.empty() ? "No music" : "Music");
        lv_label_set_text(s_sub, s_lists.empty() ? (sd_ok() ? "Put MP3s in /music on the card, or press s to sync" : "Needs an SD card") : "click to play");
    }
    uint32_t sec = s_out && s_out->frames ? s_out->frames / s_out->rate() : 0;
    const char *state = s_error[0] ? s_error : !s_playing ? LV_SYMBOL_STOP " stopped" : s_paused ? LV_SYMBOL_PAUSE " paused" : LV_SYMBOL_PLAY " playing";
    lv_label_set_text_fmt(s_time, "%s   %lu:%02lu   " LV_SYMBOL_VOLUME_MAX " %d%%%s%s", state, (unsigned long)(sec / 60), (unsigned long)(sec % 60), s_volume,
                          s_syncing ? "   " : "", s_syncing ? s_sync_status : "");
}

static void show_now()
{
    s_view = NOW;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    s_big = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_big, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_big, T_ACCENT, 0);
    lv_obj_set_width(s_big, LV_PCT(100));
    lv_label_set_long_mode(s_big, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_big, 4, 24);
    s_sub = lv_label_create(s_root);
    lv_obj_set_style_text_color(s_sub, T_DIM, 0);
    lv_obj_set_width(s_sub, LV_PCT(100));
    lv_label_set_long_mode(s_sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_sub, 4, 100);
    s_time = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_14, 0); // the theme's font is too wide for this line
    lv_obj_set_style_text_color(s_time, T_DIM, 0);
    lv_obj_set_pos(s_time, 4, 150);
    s_hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hint, T_DIM, 0);
    lv_label_set_text(s_hint, "click = play/pause   < > = track   up/down = volume\np = playlists   x = stop   s = sync from dongle");
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_LEFT, 0, -2);
    refresh_now();
}

static void list_clicked(lv_event_t *e) { show_tracks((int)(intptr_t)lv_event_get_user_data(e)); }
static void track_clicked(lv_event_t *e)
{
    play_track(s_browse, (int)(intptr_t)lv_event_get_user_data(e));
    show_now();
}

static void show_lists()
{
    s_view = LISTS;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    lv_obj_t *list = lv_list_create(s_root);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    ui_list_text(list, "Playlists  (folders in /music)");
    for (size_t i = 0; i < s_lists.size(); i++) {
        char line[80];
        snprintf(line, sizeof(line), "%s   %d", s_lists[i].name.c_str(), (int)s_lists[i].files.size());
        lv_obj_t *b = ui_list_btn(list, (int)i == s_list ? LV_SYMBOL_PLAY : LV_SYMBOL_AUDIO, line);
        lv_obj_add_event_cb(b, list_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (s_lists.empty()) ui_list_text(list, sd_ok() ? "(none yet)" : "(no SD card)");
}

static void show_tracks(int list)
{
    s_view = TRACKS;
    s_browse = list;
    lv_obj_clean(s_root);
    lv_group_remove_all_objs(ui_group());
    lv_obj_t *l = lv_list_create(s_root);
    lv_obj_set_size(l, LV_PCT(100), LV_PCT(100));
    ui_list_text(l, s_lists[list].name.c_str());
    for (size_t i = 0; i < s_lists[list].files.size(); i++) {
        bool cur = list == s_list && (int)i == s_index;
        lv_obj_t *b = ui_list_btn(l, cur ? LV_SYMBOL_PLAY : LV_SYMBOL_AUDIO, pretty(s_lists[list].files[i]).c_str());
        lv_obj_add_event_cb(b, track_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void save_volume()
{
    Preferences p;
    p.begin("music", false);
    p.putUChar("vol", s_volume);
    p.end();
}

static bool keys(uint32_t k)
{
    if (s_view == NOW) {
        if (k == LV_KEY_ENTER) toggle();
        else if (k == LV_KEY_RIGHT) next_track(1);
        else if (k == LV_KEY_LEFT) next_track(-1);
        else if (k == LV_KEY_PREV || k == LV_KEY_NEXT) {
            s_volume += k == LV_KEY_PREV ? 5 : -5;
            s_volume = s_volume < 0 ? 0 : s_volume > 100 ? 100 : s_volume;
            apply_volume();
            save_volume();
        }
        else if (k == 'x') stop_track();
        else if (k == 'p') { show_lists(); return true; }
        else if (k == 's') sync_start();
        else return false;
        refresh_now();
        return true;
    }
    if (k == 's') { sync_start(); return true; }
    return false;
}

static void open(lv_obj_t *parent)
{
    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    Preferences p;
    p.begin("music", true);
    s_volume = p.getUChar("vol", 60);
    p.end();
    if (s_lists.empty()) scan();
    ui_set_app_key_hook(keys);
    show_now();
}

static void tick()
{
    static uint32_t last = 0;
    if (millis() - last > 500) { last = millis(); refresh_now(); }
}

static bool on_back()
{
    if (s_view == TRACKS) { show_lists(); return true; }
    if (s_view == LISTS) { show_now(); return true; }
    return false;
}

static void close() { s_root = nullptr; }

const App app_music = {"Music", LV_SYMBOL_AUDIO, open, close, tick, on_back};
