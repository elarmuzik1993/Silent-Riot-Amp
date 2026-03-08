#pragma once
// Cabinet Sim (Spec §3.10) — IIR fallback + future IR loader
#include "../filters/FilterBank.h"

namespace tt {

struct CabinetSim {
    bool enabled = true;
    // IIR cab filters are in FilterBank (cab_presence, cab_rolloff, cab_notch)
    // IR convolution engine: TODO Phase 4
    void setEnabled(bool e) { enabled = e; }
};

} // namespace tt
