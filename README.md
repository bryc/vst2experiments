# vst2experiments

I'm trying to learn how VST interfacing works at a low-level. Initially it seemed very possible to create working VST2 plugins with minimal code, so I decided I would explore this route before abstracting further with the multitude of available VST2 frameworks. The VST specification part is actually the easier part. The difficult part is actually writing and understanding all the parts of the synth code from scratch.

## Projects covered

For now I'm focusing on basic synth design using sample data to play notes with.

* Wavetable synth (4-bit, 32-sample waveforms. GB inspired)
* Generic sample player with loop points
