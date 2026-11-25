# Glossary {#glossary}

<!-- Copyright 2026 Timothy Place and the catenae contributors -->

The one glossary (PLAN.md section 11.2, rule 6). Every page uses these words in these senses.

| Word | Meaning |
|---|---|
| **frame** | One sample of each channel, at one instant: `Ch` values of an audio port, `Ch` x `Bins` of a spectrum. |
| **port** | The type of what a stage reads or writes: its kind (audio, spectrum, wire), sample format, channel count and layout, all compile-time. |
| **stage** | A unit of processing, read through `stage_traits`: a configuration, with a state and a workspace beside it (@ref stage_contract). |
| **configuration** | A stage's parameters: a small, trivially copyable constant, at most 512 bytes (D34, D54). |
| **state** | What one instance of a stage remembers between calls: plain storage, apart from the configuration. All zero is "not yet reset". |
| **workspace** | What one call needs and nothing keeps: plain storage, never reset (D50). |
| **reset** | Bringing a state to rest, in place: zeroed when the stage rests at zero, by its `reset` otherwise (D49). |
| **chain** | Stages composed with `a \| b \| c` into one stage, its joints proven when its type is formed. |
| **junction** | Where two call sites meet, one thread each side, their clocks shared or independent (@ref junction_contract). |
| **mailbox** | A value carried from one thread to another, the latest replacing the last (D44). |
| **input group** | The smallest number of input frames after which a stage's output keeps pace exactly; 1 for a frame-synchronous stage. |
| **shortfall** | How far a stage's output trails the input times its ratio, at most `max_shortfall`. |
| **lead** | How far it runs ahead, at most `max_lead`. |
| **design** | The coefficients a stage runs on, computed once by `make()`, outside the audio path, which can fail with a `status`. |
| **tail** | The frames of silent input after which a stage's own output is silence within its residue. |
| **residue** | The level left after the tail: 0 for a finite impulse response, a stated bound for a recursion. |
