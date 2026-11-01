# Mutable Instruments DSP (vendored)

DSP code from Mutable Instruments' open-source Eurorack firmware, used for the
PLAITS, BRAIDS and RINGS synth engines.

- Source: https://github.com/pichenettes/eurorack (commit
  `f4cfa7697095a28d292bff758beb51deb49b5a86`)
- Support library: https://github.com/pichenettes/stmlib (commit
  `d18def816c51d1da0c108236928b2bbd25c17481`)
- License: MIT, Copyright (c) Emilie Gillet. See `stmlib/LICENSE`; the same
  notice applies to the `plaits`, `braids` and `rings` sources.

Only the DSP, lookup-table and helper sources are included. Firmware, UI,
drivers, hardware and test code are not.

## Local modifications

1. `plaits/dsp/voice.cc`: removed the `plaits/user_data.h` include. The
   user-data flash block does not exist here, so the 6-op FM engines always use
   the built-in patch table.
2. `stmlib/stmlib.h`: `IN_RAM` expands to nothing unless
   `MUTABLE_EMBEDDED_TARGET` is defined (it is a linker-section attribute on
   the original STM32 target).
3. `stmlib/dsp/dsp.h`: `Clip16`, `ClipU16` and `Sqrt` use the portable C++ path
   unless `MUTABLE_EMBEDDED_TARGET` is defined, instead of ARM inline assembly.

The `mutable_dsp` CMake target compiles these sources with warnings disabled;
the app wrappers live in `src/audio/{Plaits,Braids,Rings}Voice.cpp`.
