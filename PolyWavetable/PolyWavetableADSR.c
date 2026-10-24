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

enum { P_WAVE, P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE,
NUM_PARAMS }; // keep last

enum { V_IDLE, V_ATK, V_DEC, V_SUS, V_REL }; // env states

typedef struct {
    uint8_t numWaves;
    uint8_t reserved;
    uint8_t bitDepth;
    uint8_t numSamples;
    uint8_t wave[MAX_WAVES][MAX_SAMPLES];
    float waveIndex;
    float attackTime, decayTime, sustainLevel, releaseTime;
} SaveState;

typedef struct {
    AEffect eff;
    SaveState state;
    float fwave[MAX_WAVES][MAX_SAMPLES];
    float baseStep, pitchBend, sampleRate;
    float phase[VOICES], step[VOICES];
    int8_t note[VOICES], waveID;
    uint32_t age[VOICES], ageClock;
    // ADSR
    float atkIncr, decCoef, relCoef, envGain[VOICES], vel[VOICES];
    int8_t envStage[VOICES];
} Plugin;

static void updateEnv(Plugin *p, int32_t idx) {
    if (idx == 1 || idx == -1) { 
        float n = p->state.attackTime * p->state.attackTime * 10 * p->sampleRate;
        p->atkIncr = n < 1.0f ? 1.0f : 1.0f / n;
    }
    if (idx == 2 || idx == -1) p->decCoef = exp2f(-1.0f / (p->state.decayTime * p->sampleRate));
    if (idx == 4 || idx == -1) p->relCoef = exp2f(-1.0f / (p->state.releaseTime * p->sampleRate));
}

static void setParam(AEffect *e, int32_t idx, float val) {
    Plugin *p = e->global;
    if (idx == 0) {
        p->state.waveIndex = val;
        p->waveID = val >= 1.0f ? p->state.numWaves - 1 : (int)(p->state.numWaves * val);
        return;
    }
    if (idx == 1) p->state.attackTime = val;
    if (idx == 2) p->state.decayTime = val;
    if (idx == 3) p->state.sustainLevel = val;
    if (idx == 4) p->state.releaseTime = val;
    updateEnv(p, idx);
}

static float getParam(AEffect *e, int32_t idx) {
    Plugin *p = e->global;
    switch (idx) {
        case 0: return p->state.waveIndex;
        case 1: return p->state.attackTime;
        case 2: return p->state.decayTime;
        case 3: return p->state.sustainLevel;
        case 4: return p->state.releaseTime;
    }
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
        if (p->envStage[v] == V_IDLE) continue; // Skip inactive voices
        p->step[v] = p->baseStep * exp2f((p->note[v] - 69 + p->pitchBend) / 12.0f) / OS;
    }
    for (int i = 0; i < blockSize; i++) {
        float sample = 0.0f;
        for (int o = 0; o < OS; o++) for (int v = 0; v < VOICES; v++) {
            if (p->envStage[v] == V_IDLE) continue; // Skip inactive voices
            sample += p->fwave[p->waveID][(int)(p->phase[v] * p->state.numSamples)] * p->envGain[v];
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
            uint8_t status = m->data[0] & 0xF0, d1 = m->data[1], d2 = m->data[2];

            if (status == 0x90 && d2 > 0) { // Note On
                uint8_t v, old = 0;
                for (v = 0; v < VOICES && p->envStage[v]; v++) if (p->age[v] < p->age[old]) old = v;
                if (v == VOICES) v = old; p->age[v] = ++p->ageClock;
                
                p->note[v] = d1;
                p->vel[v] = (d2 / 127.0f) * 0.3f;
                p->envStage[v] = V_ATK, p->envGain[v] = 0.0f;
                
            } else if (status == 0x80 || status == 0x90) { // Note Off
                for (int v = 0; v < VOICES; v++) {
                    if (p->envStage[v] > V_IDLE && p->envStage[v] != V_REL && d1 == p->note[v]) {
                        p->envStage[v] = V_REL; break;
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
    case 10: p->sampleRate = opt; p->baseStep = 440.0f / opt; updateEnv(p, -1); return 0; // effSetSampleRate
    case 7: // effGetParamDisplay
        if (idx == 0) snprintf(ptr, 24, "%02d / %02d [%d-bit/%d]", p->waveID+1, p->state.numWaves, p->state.bitDepth, p->state.numSamples);
        if (idx == 1) snprintf(ptr, 24, "%.2f s", p->state.attackTime * 11.0f);
        if (idx == 2) snprintf(ptr, 24, "%.1f s", p->state.decayTime * 11.0f);
        if (idx == 3) snprintf(ptr, 24, "%.0f%%", p->state.sustainLevel * 100.0f);
        if (idx == 4) snprintf(ptr, 24, "%.1f s", p->state.releaseTime * 11.0f);
        return 0;
    case 8: // effGetParamName
        if (idx == 0) strcpy(ptr, "WaveIndex");
        if (idx == 1) strcpy(ptr, "Attack");
        if (idx == 2) strcpy(ptr, "Decay");
        if (idx == 3) strcpy(ptr, "Sustain");
        if (idx == 4) strcpy(ptr, "Release");
        return idx < NUM_PARAMS;
    case 26: return idx < NUM_PARAMS; // effCanBeAutomated
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
        updateEnv(p, -1);
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
    e->numParams = NUM_PARAMS;
    e->pluginId  = 0x1337;

    p->sampleRate = 44100.0f;
    p->baseStep = 440.0f / p->sampleRate;
    p->state.waveIndex = 0.0f;
    p->state.numWaves = 1;
    p->state.bitDepth = 4;
    p->state.numSamples = 32;
    p->state.attackTime = 0.0f;
    p->state.decayTime = 0.0f;
    p->state.sustainLevel = 1.0f;
    p->state.releaseTime = 0.0f;
    updateEnv(p, -1);
    
    uint8_t defWave[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
    memcpy(p->state.wave, defWave, sizeof defWave);
    normalizeWaves(p);

    return e;
}
