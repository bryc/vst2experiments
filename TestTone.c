#include <stdint.h>
#include <stdlib.h>

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
    AEffect eff;
    float phase;
} Plugin;

static void process(AEffect *e, float **ins, float **outs, int32_t sampleFrames) {
    Plugin *p = (Plugin *)e;
    
    for (int32_t i = 0; i < sampleFrames; i++) {
        outs[0][i] = outs[1][i] = (p->phase < 0.5f) ? 0.1f : -0.1f;
        
        p->phase += 440.0f / 44100.0f;
        if (p->phase >= 1.0f) p->phase -= 1.0f;
    }
}

static intptr_t dispatch(AEffect *e, int32_t op, int32_t idx, intptr_t val, void *ptr, float opt) {
    return 0;
}

AEffect *VSTPluginMain() {
    Plugin *p = calloc(1, sizeof *p);

    p->eff.magic            = 0x56737450;
    p->eff.dispatcher       = dispatch;
    p->eff.processReplacing = process;
    p->eff.flags            = 16;
    p->eff.numOutputs       = 2;

    return &p->eff;
}
