## Notes
As of October 26, 2026, it is up and running. I'm not satisfied with the UI. There are still a lot of parts that haven't been implemented yet.
But I've run out of credits to use for coding!!!!!


Pocket Euclidean is a Euclidean-rhythm groovebox. Eight tracks each have length / fill / offset, an AD envelope and filter, LFO and jitter modulation, and a mixer with level meters. Playback is driven by the audio clock, so timing does not depend on the screen frame rate.

Samples: put mono `.wav` files in `pocketeuclidean/samples`. They are assigned to tracks 1-8 in name order. Tracks without a file use built-in sounds.

Status: this package has not been verified on hardware yet.

## Controls

| Button | Action |
|--|--|
| D-Pad Up / Down / Left / Right | Navigate / change values |
| L1 / R1 | Previous / next track |
| L2 / R2 | Previous / next screen |
| A / B | Confirm / back |
| X | Mute track |
| Y | Help |
| Start | Play / Stop |
| Select | Project menu |
| Select + Start (hold both) | Exit |

## Compile

```
cmake -S . -B build -G Ninja -DPORTMASTER_BUILD=ON \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-aarch64.cmake \
  -DPOCKET_SYSROOT=/path/to/aarch64/sysroot
cmake --build build
```

The output is `pocketeuclidean.aarch64`; place it next to the launch script's `pocketeuclidean/` data folder.
