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
    float phase, baseStep, step, pitchBend;
    uint8_t active, note;
} Plugin;

static void process(AEffect *e, float **ins, float **outs, int32_t blockSize) {
    Plugin *p = (Plugin *)e;

    for (int i = 0; i < blockSize; i++) {
        if (p->active) {
            p->step = p->baseStep * exp2f((p->note - 69 + p->pitchBend) / 12.0f);
            outs[0][i] = outs[1][i] = (p->phase < 0.5f) ? 0.1f : -0.1f;

            p->phase += p->step;
            if (p->phase >= 1.0f) p->phase -= 1.0f;
        } else {
            outs[0][i] = outs[1][i] = 0.0f;
        }
    }
}

static intptr_t dispatch(AEffect *e, int32_t opCode, int32_t idx, intptr_t val, void *ptr, float opt) {
    Plugin *p = (Plugin *)e;
    
    switch (opCode) {
    case 25: {
        VstEvents *ve = (VstEvents *)ptr;
        for (int i = 0; i < ve->numEvents; i++) {
            VstMidiEvent *evt = (VstMidiEvent *)ve->events[i];
            if (evt->type != 1) continue; // Only process MIDI type events

            uint8_t status = evt->data[0] & 0xF0, LSB = evt->data[1], MSB = evt->data[2];

            if (status == 0x90 && MSB > 0) {
                p->active = 1; // Note On
                p->note = LSB;
            } else if ((status == 0x80 || status == 0x90) && LSB == p->note)  {
                p->active = 0; // Note Off
            } else if(status == 0xE0) { /* Pitch bend */
                uint16_t pitchBendRaw = (MSB << 7) | LSB;
                float pitchBendNorm = (pitchBendRaw - 8192) / 8192.0f;
                p->pitchBend = pitchBendNorm * 12.0f;
            }
        }
    }
    case 51: return !strcmp(ptr, "receiveVstMidiEvent");
    default: return 0;
    }
}

AEffect *VSTPluginMain() {
    Plugin *p = calloc(1, sizeof *p);

    p->eff.magic            = 0x56737450;
    p->eff.dispatcher       = dispatch;
    p->eff.processReplacing = process;
    p->eff.flags            = 16 | 256;
    p->eff.numOutputs       = 2;

    p->baseStep = 440.0f / 44100.0f;

    return &p->eff;
}
