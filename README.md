# Orange Tiny Terror — DSP Amp Model

**Monolit Beatz** amp sim plugin — full analog circuit model of the Orange AD5 Tiny Terror.

Built with [JUCE 7](https://juce.com/) + CMake on Ubuntu Linux.

## Quick Start

```bash
./scripts/install-deps.sh    # Ubuntu packages (one-time)
./scripts/build.sh           # Debug build
./scripts/build.sh release   # Release build
```

## Architecture (Spec v2.6)

The entire signal chain operates in a **normalized voltage domain** where 300V B+ = 1.0.
Every component value traces back to real-world measurements through `VoltageScale`.

```
src/
├── utils/
│   ├── VoltageScale.h      # Normalized domain framework (§2.5)
│   ├── Constants.h          # All DSP constants from spec §10
│   ├── Parameters.h         # APVTS parameter IDs
│   └── ParamSmoother.h      # Exponential parameter smoothing
├── dsp/
│   ├── filters/
│   │   ├── OnePole.h        # BLT-correct 1st order LP/HP (§2.6.2)
│   │   ├── Biquad.h         # TDF-II biquad, RBJ cookbook (§2.6.2)
│   │   ├── FilterBank.h/cpp # Central filter management — 24 instances
│   │   └── DynamicMillerLP.h # Gain-dependent Miller cap rolloff (§3.3)
│   ├── tubes/
│   │   ├── TriodeWaveshaper.h/cpp  # 12AX7 model + LTP current (§4.1)
│   │   ├── PentodeWaveshaper.h/cpp # EL84 screen-aware model (§4.2)
│   │   └── GridConduction.h/cpp    # Per-stage grid loading (§4.1.1)
│   ├── power/
│   │   ├── PowerSupplyModel.h/cpp  # 4-node multi-stage PSU droop (§3.11)
│   │   ├── SagEnvelope.h           # Cathode bias sag (§3.8)
│   │   └── LTPSolver.h/cpp        # Nodal LTP phase inverter (§3.5)
│   ├── cabinet/
│   │   ├── SpeakerCoupling.h/cpp   # Dynamic load-line interaction (§3.9)
│   │   └── CabinetSim.h/cpp       # IIR cab + future IR loader (§3.10)
│   └── OversampleSwitcher.h/cpp    # Crossfade state machine (§5.1)
├── gui/
│   ├── LookAndFeel.h/cpp   # Orange-inspired dark theme
│   └── AmpKnob.h/cpp       # Rotary knob component
├── PluginProcessor.h/cpp    # Full signal chain (§2.5 processBlock)
└── PluginEditor.h/cpp       # 3-knob GUI: Gain, Tone, Volume
```

## Implementation Phases

- [x] **Phase 1:** Core preamp — VoltageScale, triode waveshaper, cascaded stages, gain control, basic OS
- [ ] **Phase 2:** Power section — LTP nodal solver, tone control, EL84 pentode, push-pull
- [ ] **Phase 3:** Dynamics — PSU multi-node droop, sag, grid conduction, speaker coupling
- [ ] **Phase 4:** Cabinet & polish — IR loader, GUI, presets

## Git Workflow

```
main ────────── stable releases
  └─ develop ── active work
       ├─ feature/phase-1-preamp
       ├─ feature/phase-2-power
       └─ fix/filter-coefficients
```
