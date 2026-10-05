// PC test-harness platform: runs the game headless on top of gxemu, drives
// input from a script and writes screenshots / audio for inspection.
#ifdef HV_PC
#include "platform/platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <algorithm>
#include <string>
#include <vector>

void *hvAlignedAlloc(size_t size) {
    void *p = nullptr;
    if (posix_memalign(&p, 32, (size + 31) & ~(size_t)31) != 0) return nullptr;
    return p;
}
void hvAlignedFree(void *p) { free(p); }

void hvLog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[hv] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

namespace {

struct Event {
    u32 frame;
    std::string cmd;
    std::vector<std::string> args;
};

std::vector<Event> g_events;
size_t g_nextEvent = 0;
u32 g_frame = 0;
u32 g_maxFrames = 600;
std::string g_pakPath = "data/assets.pak";
std::string g_savePath = "out/save.bin";
std::string g_wavPath;
bool g_rasterAll = false;
bool g_quit = false;
PadState g_pad;
u16 g_heldNext = 0, g_pulse = 0;
std::vector<std::pair<u32, std::string>> g_shots;  // frame, path
u8 *g_pakAligned = nullptr;
plat::AudioCallback g_audioCb = nullptr;
void *g_audioUser = nullptr;
std::vector<s16> g_wav;

u16 buttonFromName(const std::string &n) {
    static const struct { const char *n; u16 b; } tab[] = {
        {"A", BTN_A}, {"B", BTN_B}, {"X", BTN_X}, {"Y", BTN_Y}, {"Z", BTN_Z}, {"L", BTN_L}, {"R", BTN_R},
        {"START", BTN_START}, {"UP", BTN_UP}, {"DOWN", BTN_DOWN}, {"LEFT", BTN_LEFT}, {"RIGHT", BTN_RIGHT}};
    for (auto &t : tab)
        if (n == t.n) return t.b;
    fprintf(stderr, "unknown button %s\n", n.c_str());
    return 0;
}

void loadScript(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot open script %s\n", path);
        exit(1);
    }
    char line[512];
    u32 base = 0;
    while (fgets(line, sizeof(line), f)) {
        char *h = strchr(line, '#');
        if (h) *h = 0;
        std::vector<std::string> tok;
        char *s = strtok(line, " \t\r\n");
        while (s) {
            tok.push_back(s);
            s = strtok(nullptr, " \t\r\n");
        }
        if (tok.empty()) continue;
        // "<frame> cmd args" or "+<delta> cmd args" (relative to previous line)
        u32 fr;
        if (tok[0][0] == '+') fr = base + (u32)atoi(tok[0].c_str() + 1);
        else fr = (u32)atoi(tok[0].c_str());
        base = fr;
        Event e;
        e.frame = fr;
        e.cmd = tok.size() > 1 ? tok[1] : "";
        for (size_t i = 2; i < tok.size(); i++) e.args.push_back(tok[i]);
        if (e.cmd == "shot" && !e.args.empty()) g_shots.push_back({fr, e.args[0]});
        g_events.push_back(e);
    }
    fclose(f);
}

void writePng(const char *path, const u8 *rgb, int w, int h) {
    std::vector<u8> raw;
    raw.reserve((size_t)(w * 3 + 1) * h);
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb + (size_t)y * w * 3, rgb + (size_t)(y + 1) * w * 3);
    }
    uLongf clen = compressBound(raw.size());
    std::vector<u8> comp(clen);
    compress2(comp.data(), &clen, raw.data(), raw.size(), 6);
    comp.resize(clen);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    auto be = [](u32 v, u8 *o) { o[0] = v >> 24; o[1] = v >> 16; o[2] = v >> 8; o[3] = v; };
    auto chunk = [&](const char *type, const u8 *data, u32 len) {
        u8 hdr[8];
        be(len, hdr);
        memcpy(hdr + 4, type, 4);
        fwrite(hdr, 1, 8, f);
        if (len) fwrite(data, 1, len, f);
        u32 crc = crc32(0, (const Bytef *)type, 4);
        if (len) crc = crc32(crc, data, len);
        u8 c[4];
        be(crc, c);
        fwrite(c, 1, 4, f);
    };
    static const u8 sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    u8 ihdr[13];
    be(w, ihdr);
    be(h, ihdr + 4);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", comp.data(), (u32)comp.size());
    chunk("IEND", nullptr, 0);
    fclose(f);
    fprintf(stderr, "[hv] wrote %s\n", path);
}

void writeWav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    u32 dataBytes = (u32)g_wav.size() * 2;
    auto w32 = [&](u32 v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](u16 v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); w32(16); w16(1); w16(2); w32(plat::AUDIO_RATE); w32(plat::AUDIO_RATE * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(dataBytes);
    fwrite(g_wav.data(), 2, g_wav.size(), f);
    fclose(f);
    fprintf(stderr, "[hv] wrote %s (%.1fs)\n", path, g_wav.size() / 2.0 / plat::AUDIO_RATE);
}

}  // namespace

namespace plat {

void setArgs(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char * { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--script") loadScript(next());
        else if (a == "--frames") g_maxFrames = (u32)atoi(next());
        else if (a == "--pak") g_pakPath = next();
        else if (a == "--save") g_savePath = next();
        else if (a == "--wav") g_wavPath = next();
        else if (a == "--raster-all") g_rasterAll = true;
        else if (a == "--shot") {
            u32 fr = (u32)atoi(next());
            g_shots.push_back({fr, next()});
            Event e;
            e.frame = fr;
            e.cmd = "shot";
            g_events.push_back(e);
        } else {
            fprintf(stderr, "unknown option %s\n", a.c_str());
        }
    }
    std::stable_sort(g_events.begin(), g_events.end(), [](const Event &a, const Event &b) { return a.frame < b.frame; });
}

void init() {
    void *fifo = hvAlignedAlloc(256 * 1024);
    GX_Init(fifo, 256 * 1024);
    GXColor black = {0, 0, 0, 255};
    GX_SetCopyClear(black, 0x00FFFFFF);
    GX_SetViewport(0, 0, 640, 480, 0, 1);
    GX_SetScissor(0, 0, 640, 480);
    GX_SetDispCopySrc(0, 0, 640, 480);
    GX_SetDispCopyDst(640, 480);
    static u8 vf[7] = {0, 0, 21, 22, 21, 0, 0};
    GX_SetCopyFilter(0, nullptr, GX_TRUE, vf);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_BACK);
    gxemu_set_raster(wantsFrameRendered() ? 1 : 0);
}

void shutdown() {
    if (!g_wavPath.empty()) writeWav(g_wavPath.c_str());
}

int screenW() { return 640; }
int screenH() { return 480; }
bool widescreen() { return false; }

void endFrame() {
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(nullptr, GX_TRUE);
    GX_DrawDone();
    for (auto &s : g_shots) {
        if (s.first == g_frame) {
            int w, h;
            const u8 *rgb = gxemu_last_frame(&w, &h);
            std::string dir = s.second.substr(0, s.second.find_last_of('/'));
            if (dir != s.second) {
                std::string cmd = "mkdir -p '" + dir + "'";
                if (system(cmd.c_str()) != 0) {}
            }
            writePng(s.second.c_str(), rgb, w, h);
        }
    }
    if (g_audioCb && !g_wavPath.empty()) {
        u32 frames = AUDIO_RATE / 60;
        size_t o = g_wav.size();
        g_wav.resize(o + frames * 2);
        g_audioCb(&g_wav[o], frames, g_audioUser);
    }
    g_frame++;
    if (g_frame >= g_maxFrames) g_quit = true;
    gxemu_set_raster(wantsFrameRendered() ? 1 : 0);
}

f32 frameDelta() { return 1.0f / 60.0f; }
u32 frameCount() { return g_frame; }
u64 timeMicros() { return (u64)g_frame * 16667; }

void pollInput(PadState &p) {
    u16 prev = g_pad.held;
    g_pulse = 0;
    while (g_nextEvent < g_events.size() && g_events[g_nextEvent].frame <= g_frame) {
        Event &e = g_events[g_nextEvent++];
        if (e.cmd == "stick" && e.args.size() >= 2) { g_pad.sx = (f32)atof(e.args[0].c_str()); g_pad.sy = (f32)atof(e.args[1].c_str()); }
        else if (e.cmd == "cstick" && e.args.size() >= 2) { g_pad.cx = (f32)atof(e.args[0].c_str()); g_pad.cy = (f32)atof(e.args[1].c_str()); }
        else if (e.cmd == "hold" && !e.args.empty()) g_heldNext |= buttonFromName(e.args[0]);
        else if (e.cmd == "release" && !e.args.empty()) g_heldNext &= ~buttonFromName(e.args[0]);
        else if (e.cmd == "press" && !e.args.empty()) g_pulse |= buttonFromName(e.args[0]);
        else if (e.cmd == "trigger" && e.args.size() >= 2) {
            f32 v = (f32)atof(e.args[1].c_str());
            if (e.args[0] == "L") g_pad.l = v; else g_pad.r = v;
        }
        else if (e.cmd == "quit") g_quit = true;
        else if (e.cmd == "log") {
            std::string m;
            for (auto &a : e.args) m += a + " ";
            fprintf(stderr, "[script] %s\n", m.c_str());
        }
    }
    g_pad.held = g_heldNext | g_pulse;
    g_pad.pressed = g_pad.held & ~prev;
    g_pad.released = prev & ~g_pad.held;
    g_pad.connected = true;
    p = g_pad;
}

void rumble(bool) {}

bool saveWrite(const void *data, u32 size) {
    if (system("mkdir -p out") != 0) {}
    FILE *f = fopen(g_savePath.c_str(), "wb");
    if (!f) return false;
    fwrite(data, 1, size, f);
    fclose(f);
    return true;
}

s32 saveRead(void *data, u32 maxSize) {
    FILE *f = fopen(g_savePath.c_str(), "rb");
    if (!f) return -1;
    s32 n = (s32)fread(data, 1, maxSize, f);
    fclose(f);
    return n;
}

bool saveExists() {
    FILE *f = fopen(g_savePath.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

const u8 *assetData(u32 *size) {
    static u32 s_size = 0;
    if (!g_pakAligned) {
        FILE *f = fopen(g_pakPath.c_str(), "rb");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", g_pakPath.c_str());
            exit(1);
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        g_pakAligned = (u8 *)hvAlignedAlloc((size_t)n);
        if (fread(g_pakAligned, 1, (size_t)n, f) != (size_t)n) exit(1);
        fclose(f);
        s_size = (u32)n;
    }
    *size = s_size;
    return g_pakAligned;
}

void audioStart(AudioCallback cb, void *user) {
    g_audioCb = cb;
    g_audioUser = user;
}

bool quitRequested() { return g_quit; }

bool wantsFrameRendered() {
    if (g_rasterAll) return true;
    for (auto &s : g_shots)
        if (s.first == g_frame) return true;
    return false;
}

}  // namespace plat
#endif
