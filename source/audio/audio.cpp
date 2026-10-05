// Audio: every sound is synthesized at boot (no sample data on the disc) and
// the music is played by a small sequencer with a handful of soft instruments.
// Mixing runs on the platform audio thread; the game thread only posts
// commands through a lock-free ring.
#include <math.h>
#include <string.h>
#include "game/game.h"

namespace {

const int RATE = plat::AUDIO_RATE;
const int SFX_RATE = 24000;    // effects are rendered at half rate and resampled
const f32 TWO_PI = 6.2831853f;

// ------------------------------------------------------------- helpers
u32 s_noise = 0x1234567u;
inline f32 noise() {
    s_noise = s_noise * 1664525u + 1013904223u;
    return (f32)(s32)s_noise * (1.0f / 2147483648.0f);
}

const int SINE_N = 1024;
f32 s_sine[SINE_N + 1];
inline f32 sinT(f32 phase) {   // phase in cycles
    phase -= floorf(phase);
    f32 f = phase * SINE_N;
    int i = (int)f;
    f32 t = f - i;
    return s_sine[i] + (s_sine[i + 1] - s_sine[i]) * t;
}
inline f32 tri(f32 phase) {
    phase -= floorf(phase);
    return phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f;
}
inline f32 midiHz(f32 n) { return 440.0f * powf(2.0f, (n - 69.0f) / 12.0f); }

// ------------------------------------------------------------- effects
struct Sample {
    s16 *data;
    u32 len;
};
Sample s_sfx[SFX_COUNT];

struct Synth {
    f32 *buf;
    int n;
    explicit Synth(f32 seconds) {
        n = (int)(seconds * SFX_RATE);
        buf = new f32[n];
        memset(buf, 0, sizeof(f32) * n);
    }
    f32 t(int i) const { return (f32)i / SFX_RATE; }
    // decaying tone; glide from f0 to f1, optional harmonics
    void tone(f32 start, f32 dur, f32 f0, f32 f1, f32 amp, f32 decay, int wave = 0, f32 attack = 0.004f) {
        int a = (int)(start * SFX_RATE), b = hvMin(n, (int)((start + dur) * SFX_RATE));
        f32 ph = 0;
        for (int i = a; i < b; i++) {
            f32 lt = (f32)(i - a) / SFX_RATE;
            f32 k = lt / dur;
            f32 f = f0 + (f1 - f0) * k;
            ph += f / SFX_RATE;
            f32 env = expf(-lt * decay) * hvMin(1.0f, lt / attack) * hvMin(1.0f, (dur - lt) * 60.0f);
            f32 v = wave == 0 ? sinT(ph) : (wave == 1 ? tri(ph) : (sinT(ph) * 0.7f + sinT(ph * 2.01f) * 0.2f + sinT(ph * 3.0f) * 0.1f));
            buf[i] += v * amp * env;
        }
    }
    // filtered noise burst: lp = lowpass coefficient (0..1, higher = brighter)
    void hiss(f32 start, f32 dur, f32 amp, f32 decay, f32 lp, f32 lpEnd = -1, f32 attack = 0.002f) {
        if (lpEnd < 0) lpEnd = lp;
        int a = (int)(start * SFX_RATE), b = hvMin(n, (int)((start + dur) * SFX_RATE));
        f32 y = 0, y2 = 0;
        for (int i = a; i < b; i++) {
            f32 lt = (f32)(i - a) / SFX_RATE;
            f32 c = lp + (lpEnd - lp) * (lt / dur);
            y += (noise() - y) * c;
            y2 += (y - y2) * c;
            f32 env = expf(-lt * decay) * hvMin(1.0f, lt / attack) * hvMin(1.0f, (dur - lt) * 60.0f);
            buf[i] += y2 * amp * env * 2.0f;
        }
    }
    // metallic strike: inharmonic partials
    void bell(f32 start, f32 f, f32 amp, f32 decay, f32 dur = 0.6f) {
        static const f32 ratios[4] = {1.0f, 2.76f, 5.40f, 8.93f};
        static const f32 gains[4] = {1.0f, 0.5f, 0.25f, 0.12f};
        for (int k = 0; k < 4; k++) tone(start, dur, f * ratios[k], f * ratios[k], amp * gains[k], decay * (1.0f + k * 0.8f), 0, 0.001f);
    }
    Sample finish(f32 gain = 1.0f) {
        f32 peak = 0.0001f;
        for (int i = 0; i < n; i++) peak = hvMax(peak, fabsf(buf[i]));
        f32 g = gain * 0.9f / peak;
        Sample s;
        s.len = (u32)n;
        s.data = new s16[n];
        for (int i = 0; i < n; i++) s.data[i] = (s16)hvClamp(buf[i] * g * 32767.0f, -32767.0f, 32767.0f);
        delete[] buf;
        return s;
    }
};

void buildSfx() {
    {   Synth s(0.22f);   // chop: woody knock
        s.tone(0, 0.2f, 190, 90, 1.0f, 28);
        s.hiss(0, 0.12f, 0.8f, 40, 0.35f, 0.08f);
        s.tone(0.004f, 0.08f, 820, 600, 0.25f, 60, 2);
        s_sfx[SFX_CHOP] = s.finish(0.9f); }
    {   Synth s(0.5f);    // mine: pick on stone
        s.bell(0, 1650, 0.6f, 18, 0.45f);
        s.hiss(0, 0.06f, 1.0f, 70, 0.9f, 0.3f);
        s.tone(0, 0.12f, 140, 80, 0.5f, 35);
        s_sfx[SFX_MINE] = s.finish(0.85f); }
    {   Synth s(0.6f);    // splash
        s.hiss(0, 0.55f, 1.0f, 7, 0.55f, 0.08f, 0.01f);
        s.tone(0, 0.15f, 500, 180, 0.3f, 20);
        s_sfx[SFX_SPLASH] = s.finish(0.7f); }
    {   Synth s(0.16f);   // pick up
        s.tone(0, 0.07f, 660, 990, 0.8f, 20, 1);
        s.tone(0.06f, 0.09f, 990, 1320, 0.7f, 25, 1);
        s_sfx[SFX_PICK] = s.finish(0.7f); }
    {   Synth s(0.05f);   // ui move
        s.tone(0, 0.045f, 1500, 1400, 1, 70, 0, 0.001f);
        s_sfx[SFX_UI_MOVE] = s.finish(0.45f); }
    {   Synth s(0.2f);    // ui ok
        s.tone(0, 0.08f, 784, 784, 0.8f, 25, 1);
        s.tone(0.07f, 0.12f, 1175, 1175, 0.8f, 22, 1);
        s_sfx[SFX_UI_OK] = s.finish(0.6f); }
    {   Synth s(0.2f);    // ui back
        s.tone(0, 0.08f, 880, 880, 0.8f, 25, 1);
        s.tone(0.07f, 0.12f, 587, 587, 0.8f, 22, 1);
        s_sfx[SFX_UI_BACK] = s.finish(0.55f); }
    {   Synth s(1.4f);    // level up: rising bells
        const f32 notes[5] = {72, 76, 79, 84, 88};
        for (int i = 0; i < 5; i++) s.bell(i * 0.09f, midiHz(notes[i]), 0.6f, 4.0f, 1.2f);
        s.tone(0.36f, 1.0f, midiHz(60), midiHz(60), 0.35f, 3, 1, 0.02f);
        s.tone(0.36f, 1.0f, midiHz(67), midiHz(67), 0.25f, 3, 1, 0.02f);
        s_sfx[SFX_LEVEL] = s.finish(0.9f); }
    {   Synth s(0.2f);    // hit
        s.hiss(0, 0.15f, 1, 30, 0.6f, 0.1f);
        s.tone(0, 0.15f, 160, 60, 0.9f, 25);
        s_sfx[SFX_HIT] = s.finish(0.9f); }
    {   Synth s(0.3f);    // hurt
        s.tone(0, 0.25f, 420, 180, 0.8f, 10, 2);
        s.hiss(0, 0.1f, 0.5f, 30, 0.4f);
        s_sfx[SFX_HURT] = s.finish(0.8f); }
    {   Synth s(0.22f);   // swing: whoosh
        s.hiss(0, 0.2f, 1, 6, 0.05f, 0.45f, 0.06f);
        s_sfx[SFX_SWING] = s.finish(0.55f); }
    {   Synth s(0.7f);    // fire
        s.hiss(0, 0.65f, 1, 4, 0.15f, 0.05f, 0.03f);
        for (int i = 0; i < 9; i++) s.hiss(0.05f + i * 0.06f + noise() * 0.02f, 0.02f, 0.5f, 80, 0.9f);
        s.tone(0, 0.5f, 90, 60, 0.5f, 5);
        s_sfx[SFX_FIRE] = s.finish(0.8f); }
    {   Synth s(0.7f);    // ice: crystalline shimmer
        for (int i = 0; i < 6; i++) s.bell(i * 0.05f, midiHz(91 + (i % 3) * 4), 0.3f, 6, 0.6f);
        s.hiss(0, 0.3f, 0.3f, 10, 0.95f);
        s_sfx[SFX_ICE] = s.finish(0.6f); }
    {   Synth s(0.9f);    // heal
        const f32 notes[4] = {79, 83, 86, 91};
        for (int i = 0; i < 4; i++) s.tone(i * 0.08f, 0.6f, midiHz(notes[i]), midiHz(notes[i]), 0.5f, 5, 1, 0.01f);
        s_sfx[SFX_HEAL] = s.finish(0.6f); }
    {   Synth s(0.35f);   // craft step: hammer on anvil
        s.bell(0, 1180, 0.7f, 14, 0.35f);
        s.tone(0, 0.08f, 220, 120, 0.5f, 40);
        s_sfx[SFX_CRAFT_STEP] = s.finish(0.75f); }
    {   Synth s(1.2f);    // craft done: warm chord
        const f32 notes[4] = {67, 71, 74, 79};
        for (int i = 0; i < 4; i++) s.bell(i * 0.03f, midiHz(notes[i]), 0.4f, 3.5f, 1.1f);
        s_sfx[SFX_CRAFT_DONE] = s.finish(0.85f); }
    {   Synth s(0.4f);    // coin
        s.bell(0, midiHz(95), 0.6f, 12, 0.2f);
        s.bell(0.07f, midiHz(100), 0.7f, 9, 0.3f);
        s_sfx[SFX_COIN] = s.finish(0.6f); }
    {   Synth s(1.3f);    // quest fanfare
        const f32 notes[4] = {67, 72, 76, 79};
        const f32 at[4] = {0, 0.12f, 0.24f, 0.4f};
        for (int i = 0; i < 4; i++) s.tone(at[i], i == 3 ? 0.85f : 0.16f, midiHz(notes[i]), midiHz(notes[i]), 0.6f, i == 3 ? 2.5f : 8, 2, 0.01f);
        s.tone(0.4f, 0.85f, midiHz(55), midiHz(55), 0.4f, 2.5f, 1, 0.02f);
        s_sfx[SFX_QUEST] = s.finish(0.9f); }
    {   Synth s(0.25f);   // bite: plop
        s.tone(0, 0.2f, 900, 250, 1, 18);
        s.hiss(0.02f, 0.15f, 0.4f, 25, 0.4f);
        s_sfx[SFX_BITE] = s.finish(0.8f); }
    {   Synth s(0.3f);    // slime: wobbly squelch
        int a = 0, b = s.n;
        f32 ph = 0;
        for (int i = a; i < b; i++) {
            f32 lt = s.t(i);
            f32 f = 180 + 120 * sinT(lt * 22.0f) - lt * 200;
            ph += f / SFX_RATE;
            s.buf[i] += sinT(ph) * expf(-lt * 9) * hvMin(1.0f, lt * 200);
        }
        s.hiss(0, 0.1f, 0.3f, 30, 0.2f);
        s_sfx[SFX_SLIME] = s.finish(0.75f); }
    {   Synth s(0.3f);    // bones: dry clatter
        for (int i = 0; i < 5; i++) {
            f32 at = i * 0.045f + fabsf(noise()) * 0.02f;
            s.hiss(at, 0.03f, 0.8f, 90, 0.85f);
            s.tone(at, 0.04f, 1100 + i * 170, 900, 0.3f, 80);
        }
        s_sfx[SFX_BONES] = s.finish(0.7f); }
    {   Synth s(0.08f);   // footstep
        s.hiss(0, 0.07f, 1, 50, 0.12f);
        s.tone(0, 0.05f, 110, 70, 0.4f, 60);
        s_sfx[SFX_STEP] = s.finish(0.3f); }
    {   Synth s(0.35f);   // fail
        s.tone(0, 0.14f, 330, 300, 0.7f, 6, 2);
        s.tone(0.14f, 0.2f, 247, 220, 0.7f, 8, 2);
        s_sfx[SFX_FAIL] = s.finish(0.6f); }
}

// ------------------------------------------------------------- music
enum Instr { IN_HARP, IN_FLUTE, IN_PAD, IN_BASS, IN_BELL, IN_PLUCK, IN_DRUM, IN_HAT };

struct Note {
    bool on;
    u8 instr;
    f32 freq, phase, amp, t, len, pan;
};
const int MAX_NOTES = 24;

struct Track {
    f32 bpm;
    int key;                 // midi root
    bool minor;
    const s8 *prog;          // chord roots in scale degrees, 8 bars
    u8 lead, chords, bass;   // instruments
    bool drums;
    u32 seed;
    f32 density;             // melody note probability
    f32 volume;
};

const s8 PROG_A[8] = {0, 4, 5, 3, 0, 4, 3, 4};   // I V vi IV ...
const s8 PROG_B[8] = {0, 3, 4, 0, 5, 3, 1, 4};
const s8 PROG_C[8] = {0, 5, 3, 4, 0, 5, 3, 4};
const s8 PROG_D[8] = {0, 0, 3, 3, 5, 5, 4, 4};

const Track TRACKS[] = {
    {0, 0, false, PROG_A, 0, 0, 0, false, 0, 0, 0},                                   // none
    {70, 62, false, PROG_A, IN_HARP, IN_PAD, IN_BASS, false, 11, 0.45f, 0.9f},      // title
    {100, 67, false, PROG_B, IN_PLUCK, IN_HARP, IN_BASS, true, 23, 0.6f, 0.8f},     // town
    {88, 65, false, PROG_C, IN_FLUTE, IN_HARP, IN_BASS, false, 37, 0.5f, 0.8f},     // field
    {62, 57, true, PROG_D, IN_BELL, IN_PAD, IN_BASS, false, 41, 0.3f, 0.75f},       // night
    {138, 64, true, PROG_D, IN_PLUCK, IN_PLUCK, IN_BASS, true, 53, 0.75f, 0.8f},    // battle
    {124, 60, false, PROG_A, IN_FLUTE, IN_PLUCK, IN_BASS, true, 67, 0.75f, 0.85f},  // festival
};

const int MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
const int MINOR[7] = {0, 2, 3, 5, 7, 8, 10};

struct Music {
    int track = 0;
    f32 beatPos = 0;   // in 16th notes
    int step = -1;
    int lastDegree = 2;
    u32 rng = 1;
    f32 gain = 0, target = 0;
    Note notes[MAX_NOTES];
};
Music s_mus;

int scaleNote(const Track &t, int degree) {
    const int *sc = t.minor ? MINOR : MAJOR;
    int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
    int d = degree - oct * 7;
    return t.key + oct * 12 + sc[d];
}

u32 mrand() {
    s_mus.rng = s_mus.rng * 1103515245u + 12345u;
    return (s_mus.rng >> 16) & 0x7FFF;
}
f32 mfrand() { return mrand() / 32767.0f; }

void noteOn(int instr, f32 midi, f32 amp, f32 len, f32 pan) {
    Note *slot = nullptr;
    for (Note &n : s_mus.notes)
        if (!n.on) { slot = &n; break; }
    if (!slot) {
        slot = &s_mus.notes[0];
        for (Note &n : s_mus.notes)
            if (n.t > slot->t) slot = &n;
    }
    slot->on = true;
    slot->instr = (u8)instr;
    slot->freq = midiHz(midi);
    slot->phase = 0;
    slot->amp = amp;
    slot->t = 0;
    slot->len = len;
    slot->pan = pan;
}

// One 16th-note step of the current track. The melody is generated from a
// seeded walk over the scale, re-seeded every 8 bars so phrases repeat.
void sequencerStep(int step) {
    const Track &t = TRACKS[s_mus.track];
    int bar = (step / 16) % 8;
    int s16 = step % 16;
    if (s16 == 0 && bar == 0) s_mus.rng = t.seed * 7919u + (u32)(step / 128 % 2) * 131u;
    f32 beat = 60.0f / t.bpm, sixteenth = beat / 4;
    int root = t.prog[bar];
    // chords: on the bar (pads) or as arpeggios
    if (t.chords == IN_PAD) {
        if (s16 == 0)
            for (int k = 0; k < 3; k++) noteOn(IN_PAD, scaleNote(t, root + k * 2), 0.16f, beat * 4, k == 1 ? 0.5f : (k ? 0.75f : 0.25f));
    } else {
        static const int ARP[8] = {0, 2, 4, 7, 4, 2, 0, 2};
        if (s16 % 2 == 0) noteOn(t.chords, scaleNote(t, root + ARP[(s16 / 2) % 8]), 0.13f, sixteenth * 3, 0.3f + 0.4f * ((s16 / 2) % 2));
    }
    // bass
    if (s16 == 0 || s16 == 8 || (t.drums && s16 == 12)) noteOn(t.bass, scaleNote(t, root - 14 + (s16 == 8 ? 4 : 0)), 0.32f, beat * 1.6f, 0.5f);
    // melody
    bool strong = s16 % 4 == 0;
    f32 p = strong ? t.density + 0.2f : t.density * 0.45f;
    if (s16 % 2 == 0 && mfrand() < p) {
        int chordTones[3] = {root, root + 2, root + 4};
        int deg;
        if (strong) deg = chordTones[mrand() % 3] + 7;
        else deg = s_mus.lastDegree + (int)(mrand() % 3) - 1;
        // keep the line close to the previous note
        while (deg - s_mus.lastDegree > 4) deg -= 7;
        while (s_mus.lastDegree - deg > 4) deg += 7;
        deg = hvClamp(deg, 3, 13);
        s_mus.lastDegree = deg;
        f32 len = sixteenth * (strong ? 3.5f : 1.8f);
        noteOn(t.lead, scaleNote(t, deg), 0.22f, len, 0.55f);
    }
    // percussion
    if (t.drums) {
        if (s16 == 0 || s16 == 8 || (t.bpm > 130 && s16 == 10)) noteOn(IN_DRUM, 36, 0.5f, 0.25f, 0.5f);
        if (s16 % 4 == 2) noteOn(IN_HAT, 90, 0.12f, 0.05f, 0.65f);
        if (s16 == 4 || s16 == 12) noteOn(IN_HAT, 80, 0.2f, 0.12f, 0.4f);
    }
}

inline f32 renderNote(Note &n, f32 dt) {
    f32 env, v;
    f32 ph = n.phase;
    switch (n.instr) {
        case IN_HARP:
            env = expf(-n.t * 3.2f) * hvMin(1.0f, n.t * 300);
            v = sinT(ph) * 0.8f + sinT(ph * 2) * 0.25f * expf(-n.t * 8) + sinT(ph * 3) * 0.1f * expf(-n.t * 14);
            break;
        case IN_PLUCK:
            env = expf(-n.t * 7.0f) * hvMin(1.0f, n.t * 400);
            v = tri(ph) * 0.7f + sinT(ph * 2) * 0.3f * expf(-n.t * 20);
            break;
        case IN_FLUTE: {
            f32 a = hvMin(1.0f, n.t * 14), r = hvSaturate((n.len - n.t) * 10 + 1);
            env = a * r * (0.9f + 0.1f * sinT(n.t * 5.5f));
            f32 vib = 0.004f * sinT(n.t * 5.5f) * hvMin(1.0f, n.t * 3);
            v = sinT(ph * (1 + vib)) * 0.85f + sinT(ph * 2) * 0.1f + noise() * 0.02f;
            break;
        }
        case IN_PAD: {
            f32 a = hvMin(1.0f, n.t * 1.5f), r = hvSaturate((n.len - n.t) * 1.5f + 1);
            env = a * r;
            v = tri(ph) * 0.5f + tri(ph * 1.003f) * 0.5f;
            break;
        }
        case IN_BASS:
            env = expf(-n.t * 2.5f) * hvMin(1.0f, n.t * 200) * hvSaturate((n.len - n.t) * 8 + 1);
            v = sinT(ph) * 0.8f + tri(ph) * 0.3f;
            break;
        case IN_BELL:
            env = expf(-n.t * 1.8f) * hvMin(1.0f, n.t * 500);
            v = sinT(ph) * 0.7f + sinT(ph * 2.76f) * 0.25f * expf(-n.t * 3) + sinT(ph * 5.4f) * 0.1f * expf(-n.t * 6);
            break;
        case IN_DRUM: {
            env = expf(-n.t * 14);
            f32 f = 50 + 90 * expf(-n.t * 40);
            n.phase += f * dt - n.freq * dt;   // pitch sweep (compensated below)
            v = sinT(ph) + noise() * 0.1f * expf(-n.t * 60);
            break;
        }
        default:   // hat
            env = expf(-n.t * (n.len < 0.08f ? 70 : 30));
            v = noise() * 0.6f;
            break;
    }
    n.phase += n.freq * dt;
    n.t += dt;
    if (n.t > n.len + 2.0f || (n.t > n.len && env < 0.002f)) n.on = false;
    return v * env * n.amp;
}

// ------------------------------------------------------------- mixer
struct Voice {
    const Sample *s;
    u32 pos;     // 16.16 fixed point
    u32 step;
    f32 vol;
};
const int MAX_VOICES = 16;
Voice s_voices[MAX_VOICES];

// commands from the game thread (single producer / single consumer)
struct Cmd {
    u8 kind;     // 0 sfx, 1 music
    u8 id;
    f32 vol, pitch;
};
const int CMD_N = 64;
Cmd s_cmds[CMD_N];
volatile u32 s_cmdHead = 0, s_cmdTail = 0;
bool s_started = false;

f32 s_echoL[12000], s_echoR[12000];
int s_echoPos = 0;

void post(const Cmd &c) {
    u32 h = s_cmdHead;
    if (h - s_cmdTail >= CMD_N) return;   // full: drop
    s_cmds[h % CMD_N] = c;
    __sync_synchronize();
    s_cmdHead = h + 1;
}

void handleCommands() {
    while (s_cmdTail != s_cmdHead) {
        __sync_synchronize();
        Cmd c = s_cmds[s_cmdTail % CMD_N];
        s_cmdTail = s_cmdTail + 1;
        if (c.kind == 0) {
            if (c.id >= SFX_COUNT || !s_sfx[c.id].data) continue;
            Voice *v = nullptr;
            for (Voice &x : s_voices)
                if (!x.s) { v = &x; break; }
            if (!v) {   // steal the most advanced voice
                v = &s_voices[0];
                for (Voice &x : s_voices)
                    if (x.pos > v->pos) v = &x;
            }
            v->s = &s_sfx[c.id];
            v->pos = 0;
            v->step = (u32)(65536.0f * c.pitch * SFX_RATE / RATE);
            v->vol = c.vol;
        } else if (c.id != s_mus.track) {
            s_mus.target = 0;          // fade out, then switch (see mix)
            s_mus.step = -1000 - c.id; // pending track encoded here
        }
    }
}

void mix(s16 *out, u32 frames, void *) {
    handleCommands();
    const f32 dt = 1.0f / RATE;
    for (u32 i = 0; i < frames; i++) {
        // music sequencer
        f32 ml = 0, mr = 0;
        if (s_mus.step <= -1000) {
            s_mus.gain -= dt * 1.5f;
            if (s_mus.gain <= 0) {
                s_mus.gain = 0;
                s_mus.track = -1000 - s_mus.step;
                s_mus.step = -1;
                s_mus.beatPos = 0;
                for (Note &n : s_mus.notes) n.on = false;
                s_mus.target = 1;
            }
        } else if (s_mus.track > 0) {
            const Track &t = TRACKS[s_mus.track];
            s_mus.beatPos += dt * t.bpm / 60.0f * 4.0f;
            int st = (int)s_mus.beatPos;
            if (st != s_mus.step) {
                s_mus.step = st;
                sequencerStep(st);
            }
            if (s_mus.gain < s_mus.target) s_mus.gain = hvMin(s_mus.target, s_mus.gain + dt * 0.5f);
        }
        if (s_mus.track > 0 || s_mus.step <= -1000) {
            for (Note &n : s_mus.notes) {
                if (!n.on) continue;
                f32 v = renderNote(n, dt);
                ml += v * (1.0f - n.pan);
                mr += v * n.pan;
            }
            f32 g = s_mus.gain * (s_mus.track > 0 ? TRACKS[s_mus.track].volume : 0.8f) * 1.4f;
            ml *= g;
            mr *= g;
        }
        // soft stereo echo on the music for space
        int ep = s_echoPos;
        f32 el = s_echoL[ep], er = s_echoR[ep];
        s_echoL[ep] = ml * 0.5f + er * 0.38f;
        s_echoR[ep] = mr * 0.5f + el * 0.38f;
        s_echoPos = (ep + 1) % 12000;
        f32 l = ml + el * 0.45f, r = mr + er * 0.45f;
        // effects (mono, centred)
        f32 fx = 0;
        for (Voice &v : s_voices) {
            if (!v.s) continue;
            u32 idx = v.pos >> 16;
            if (idx + 1 >= v.s->len) {
                v.s = nullptr;
                continue;
            }
            f32 fr = (v.pos & 0xFFFF) * (1.0f / 65536.0f);
            f32 a = v.s->data[idx], b = v.s->data[idx + 1];
            fx += (a + (b - a) * fr) * (1.0f / 32768.0f) * v.vol;
            v.pos += v.step;
        }
        l += fx * 0.95f;
        r += fx * 0.95f;
        // gentle limiter
        l = l / (1.0f + fabsf(l) * 0.6f);
        r = r / (1.0f + fabsf(r) * 0.6f);
        out[i * 2] = (s16)hvClamp(l * 32767.0f, -32767.0f, 32767.0f);
        out[i * 2 + 1] = (s16)hvClamp(r * 32767.0f, -32767.0f, 32767.0f);
    }
}

}  // namespace

namespace audio {

void init() {
    if (s_started) return;
    for (int i = 0; i <= SINE_N; i++) s_sine[i] = sinf(TWO_PI * i / SINE_N);
    buildSfx();
    s_mus.track = 0;
    s_mus.gain = 0;
    s_mus.target = 1;
    s_started = true;
    plat::audioStart(mix, nullptr);
}

void update(f32) {}

void sfx(int id, f32 vol, f32 pitch) {
    if (!s_started) return;
    Cmd c = {0, (u8)id, vol, pitch};
    post(c);
}

void music(int track) {
    if (!s_started) return;
    static int last = -1;
    if (track == last) return;
    last = track;
    Cmd c = {1, (u8)track, 1, 1};
    post(c);
}

}  // namespace audio
