#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

typedef struct { /* VST stuff */
    int32_t magic; void *dispatch, *process, *setP, *getP;
    int32_t numPrograms, numParams, numIns, numOuts, flags;
    intptr_t r1, r2; int32_t id, rq, oq; float ir;
    void *global, *user; int32_t pluginId, version; void *processR;
} AEffect;
typedef struct {
    int32_t numEvents; intptr_t r; void *events[];
} VstEvents;
typedef struct {
    int32_t type, byteSize, dFrames, flags, noteLen, noteOff; uint8_t data[4];
} VstMidiEvent; /* VST stuff END */

#define OS 8 // Oversample factor
#define VOICES 32
#define MAX_WAVES 64
#define MAX_SAMPLES 64

typedef struct {
    uint8_t numWaves;
    uint8_t reserved;
    uint8_t bitDepth;
    uint8_t numSamples;
    uint8_t wave[MAX_WAVES][MAX_SAMPLES];
    float waveIndex;
} SaveState;

typedef struct {
    AEffect eff;
    float baseStep, pitchBend;
    float phase[VOICES], step[VOICES], gain[VOICES];
    int8_t note[VOICES], waveID;
    float fwave[MAX_WAVES][MAX_SAMPLES];
    SaveState state;
} Plugin;
    
static void setParam(AEffect *e, int32_t idx, float val) {
    Plugin *p = e->global;
    if (idx == 0) {
        p->state.waveIndex = val;
        p->waveID = val >= 1.0f ? p->state.numWaves - 1 : (int)(val * p->state.numWaves);
    }
}

static float getParam(AEffect *e, int32_t idx) {
    Plugin *p = e->global;
    return idx == 0 ? p->state.waveIndex : 0.0f;
}

static void normalizeWaves(Plugin *p) {
    const float scale = 2.0f / ((1 << p->state.bitDepth) - 1);
    for (int j = 0; j < p->state.numWaves; j++)
        for (int i = 0; i < p->state.numSamples; i++)
            p->fwave[j][i] = p->state.wave[j][i] * scale - 1.0f;
}

static void process(AEffect *e, float **ins, float **outs, int32_t blockSize) {
    Plugin *p = e->global;

    for (int v = 0; v < VOICES; v++) {
        if (p->gain[v] == 0) continue; // Skip inactive voices
        p->step[v] = p->baseStep * exp2f((p->note[v] - 69 + p->pitchBend) / 12.0f) / OS;
    }
    for (int i = 0; i < blockSize; i++) {
        float sample = 0.0f;
        for (int o = 0; o < OS; o++) for (int v = 0; v < VOICES; v++) {
            if (p->gain[v] == 0) continue; // Skip inactive voices
            sample += p->fwave[p->waveID][(int)(p->phase[v] * p->state.numSamples)] * p->gain[v];
            p->phase[v] += p->step[v];
            if (p->phase[v] >= 1.0f) p->phase[v] -= 1.0f;
        }
        outs[0][i] = outs[1][i] = sample / OS;
    }
}

static intptr_t dispatcher(AEffect *e, int32_t opCode, int32_t idx, intptr_t val, void *ptr, float opt) {
    Plugin *p = e->global;

    switch (opCode) {
    case 25: {
        VstEvents *evt = ptr;
        for (int i = 0; i < evt->numEvents; i++) {
            VstMidiEvent *m = evt->events[i];
            if (m->type != 1) continue; // Only process MIDI type events
            uint8_t status = m->data[0] & 0xF0, d1 = m->data[1], d2 = m->data[2], v;

            if (status == 0x90 && d2 > 0) { // Note On
                for (v = 0; v < VOICES; v++) {
                    if (!p->gain[v]) break;
                }
                if (v == VOICES) v = 0; // steal voice 0 if all voices in use
                p->note[v] = d1, p->gain[v] = (d2 / 127.0f) * 0.3f;
            } else if (status == 0x80 || status == 0x90) { // Note Off
                for (int v = 0; v < VOICES; v++) {
                    if (p->gain[v] && d1 == p->note[v]) {
                        p->gain[v] = 0; break;
                    }
                }
            } else if (status == 0xE0) { // Pitch bend
                uint16_t pitchBendRaw = (d2 << 7) | d1;
                float pitchBendNorm = (pitchBendRaw - 8192) / 8192.0f;
                p->pitchBend = pitchBendNorm * 12.0f;
            }
        }
        return 1;
    }
    case 10: p->baseStep = 440.0f / opt; return 0; // effSetSampleRate
    case 7: // effGetParamDisplay
        if (idx == 0) snprintf(ptr, 24, "%02d / %02d [%d-bit/%d]", p->waveID+1, p->state.numWaves, p->state.bitDepth, p->state.numSamples);
        return 1;
    case 8: // effGetParamName
        if (idx == 0) strcpy(ptr, "WaveIndex");
        return idx == 0;
    case 26: return idx == 0; // effCanBeAutomated
    case 51: return !strcmp(ptr, "receiveVstMidiEvent"); // effCanDo
    case 5:  strcpy(ptr, "WavE"); return 1; // effGetProgramName
    case 23: // effGetChunk: SAVE
        *(void **)ptr = &p->state;
        return sizeof p->state;
    case 24: // effSetChunk: LOAD
        if (!ptr) return 0;
        memcpy(&p->state, ptr, sizeof p->state);
        normalizeWaves(p);
        p->waveID = p->state.waveIndex >= 1.0f ? p->state.numWaves - 1 : (int)(p->state.waveIndex * p->state.numWaves);
        return 1;
    default: return 0;
    }
}

AEffect *VSTPluginMain() {
    Plugin *p = calloc(1, sizeof *p);
    AEffect *e = &p->eff;
    e->global    = p;
    e->dispatch  = dispatcher;
    e->processR  = process;
    e->setP      = setParam;
    e->getP      = getParam;
    e->magic     = 0x56737450;
    e->flags     = 16 | 32 | 256;
    e->numOuts   = 2;
    e->numParams = 1;
    e->pluginId  = 0x1337;

    p->baseStep = 440.0f / 44100.0f;
    p->state.waveIndex = 0.0f;
    p->state.numWaves = 1;
    p->state.bitDepth = 4;
    p->state.numSamples = 32;
    
    uint8_t defWave[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
    memcpy(p->state.wave, defWave, sizeof defWave);
    normalizeWaves(p);

    return e;
}
