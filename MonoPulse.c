#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    int32_t magic;
    void *dispatcher, *process, *setParameter, *getParameter;
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t reserved1, reserved2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueId, version;
    void *processReplacing;
} AEffect;

typedef struct {
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    uint8_t data[4];
} VstMidiEvent;

typedef struct {
    int32_t numEvents;
    intptr_t reserved;
    void *events[];
} VstEvents;

typedef struct {
    AEffect eff;
    float baseStep, baseGain, pitchBend, pulseWidth;
    float phase, step, gain;
    int8_t note;
} Plugin;

static void setParam(AEffect *e, int32_t idx, float val) {
    Plugin *p = e->object;
    if (idx == 0) p->pulseWidth = val;
}

static float getParam(AEffect *e, int32_t idx) {
    Plugin *p = e->object;
    return idx == 0 ? p->pulseWidth : 0.0f;
}

static void process(AEffect *e, float **ins, float **outs, int32_t blockSize) {
    Plugin *p = e->object;

    p->step = p->baseStep * exp2f((p->note - 69 + p->pitchBend) / 12.0f);
    for (int i = 0; i < blockSize; i++) {
        float sample = 0.0f;
        if (p->gain == 0) continue; // Skip inactive voices
        sample = (p->phase < p->pulseWidth) ? p->gain : -p->gain;
        p->phase += p->step;
        if (p->phase >= 1.0f) p->phase -= 1.0f;
        outs[0][i] = outs[1][i] = sample;
    }
}

static intptr_t dispatch(AEffect *e, int32_t opCode, int32_t idx, intptr_t val, void *ptr, float opt) {
    Plugin *p = e->object;

    switch (opCode) {
    case 25: {
        VstEvents *evt = ptr;
        for (int i = 0; i < evt->numEvents; i++) {
            VstMidiEvent *m = evt->events[i];
            if (m->type != 1) continue; // Only process MIDI type events
            uint8_t status = m->data[0] & 0xF0, d1 = m->data[1], d2 = m->data[2];

            if (status == 0x90 && d2 > 0) { // Note On
                p->note = d1, p->gain = p->baseGain * d2;
            } else if (status == 0x80 || status == 0x90) { // Note Off
                if (d1 == p->note) p->gain = 0;
            } else if (status == 0xE0) { // Pitch bend
                uint16_t pitchBendRaw = (d2 << 7) | d1;
                float pitchBendNorm = (pitchBendRaw - 8192) / 8192.0f;
                p->pitchBend = pitchBendNorm * 12.0f;
            }
        }
        return 1;
    }
    case 10: p->baseStep = 440.0f / opt; break;
    case 8:
        if (idx == 0) strcpy(ptr, "PulseWidth");
        return idx == 0;
    case 26: return idx == 0; // effCanBeAutomated
    case 51: return !strcmp(ptr, "receiveVstMidiEvent");
    default: return 0;
    }
}

AEffect *VSTPluginMain() {
    Plugin *p = calloc(1, sizeof *p);
    p->baseStep = 440.0f / 44100.0f;
    p->baseGain = 0.2f / 127.0f;
    p->pulseWidth = 0.5f;

    AEffect *e = &p->eff;
    e->object           = p;
    e->dispatcher       = dispatch;
    e->processReplacing = process;
    e->setParameter     = setParam;
    e->getParameter     = getParam;
    e->magic            = 0x56737450;
    e->flags            = 16 | 256;
    e->numOutputs       = 2;
    e->numParams        = 1;

    return e;
}
