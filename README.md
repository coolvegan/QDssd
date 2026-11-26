# QDSSD — Dynamic Spectral Sidechain Ducking

A two-plugin spectral ducker for the **Akai MPC** (standalone, OS 3.10.1+).
QDSSD removes energy from the beat **only where the vocal has energy**, so the
voice cuts through without pumping the whole mix. The kick and sub stay where
they are.

There is no host sidechain bus on the MPC, so the key travels through a
shared-memory bus between two plugins:

```
Vocal track:   [QDSSD SC Input] ──► shared-memory bus 1–8
                      │                       │
               audio untouched          chroma, notes, spectrum
                      ▼                       ▼
                   mixer                 [QDSSD] ◄── per-band mask
                                              │
                                        ducked beat
```

- **QDSSD SC Input** — put it on the vocal track. It analyses the voice and
  publishes note probabilities and a 128-band spectrum. Its audio output is
  the input, bit for bit.
- **QDSSD** — put it on the beat track or drum bus. It reads the key from the
  bus and ducks a 16-band filterbank.

## Requirements

- MPC OS **3.10.1 or later** with
  **Preferences → Enhancements (Beta) → Enable Enhancements** and
  **External Plugin Support** enabled.
- ARM32 devices: MPC Live, Live II, One, One+, X, X SE, Key 61, Key 37, Force.
- ARM64 devices: MPC Live III, MPC XL, One G2, Key G2 and newer.
- A `VST2 Plugins` folder at the root of the internal drive, an SD card or a
  USB drive.

## Installation

Copy **both** folders into `<drive>/VST2 Plugins/`:

```
VST2 Plugins/r3d0xStyle - VST - QDSSD/
VST2 Plugins/r3d0xStyle - VST - QDSSD SC Input/
```

Each folder contains `plugin.json`, `VST/<name>-arm32.so`,
`VST/<name>-arm64.so` and `Plugin Skins/`. Restart the MPC (a rescan alone may
not refresh the browser). Both appear under **External Plugins**.

Updating: delete the old folders, copy the new ones, restart.
Uninstalling: remove the folders and restart.

## Quick start

1. Vocal track → insert **QDSSD SC Input** → `To Bus 1`.
2. Beat track / drum bus → insert **QDSSD** → `Bus 1`.
3. `Depth` 24 dB, `Intensity` Extreme, `Mix` 100 %.
4. Use **Delta** to hear exactly what is being removed.

The sender track must not be muted. One sender per bus; several QDSSD
instances may read the same bus.

## Projects and the MPC screen

Loading a project now updates the MPC screen right away. QDSSD's parameters are
stored in the project, but a project load used to change the internal values
while the knobs kept their old positions. The plugin now reports the changed
parameters to the host and requests a redraw, so the screen follows the loaded
state. This is the behaviour in the 2026-11 builds.

## Controls

### QDSSD

| Control | Range | What it does |
|---|---|---|
| **Bus** | 1–8 | which shared bus to read. Must match the sender's `To Bus`. These are QDSSD buses, **not** MPC submixes. |
| **Depth** | 0–24 dB | maximum attenuation per band |
| **Speed** | 0–100 % | attack/release together: fast/dry on the left, slow/absorbing on the right (1–80 ms attack, 60–800 ms release) |
| **Mix** | 0–100 % | dry/wet |
| **Delta** | on/off | outputs only the removed signal (`dry − wet`) |
| **Intensity** | Gentle / Strong / Extreme | mask shape and overall amount |

### QDSSD SC Input

| Control | Range | What it does |
|---|---|---|
| **To Bus** | 1–8 | which bus to publish the key to |

## Programs

| Program | Bus | Depth | Speed | Mix | Intensity | Use case |
|---|---|---|---|---|---|---|
| **Vocal Clear** | 1 | 24 dB | 50 % | 100 % | Extreme | Default; the known-good vocal setup |
| **Vocal Gentle** | 1 | 12 dB | 35 % | 100 % | Gentle | Light touch on a quiet or already-bright vocal |
| **Vocal Deep** | 1 | 24 dB | 70 % | 100 % | Extreme | Slow, deep carve for dense beats |
| **Beat Duck** | 1 | 18 dB | 45 % | 100 % | Strong | Duck the beat rather than a vocal |
| **808 Safe** | 1 | 8 dB | 25 % | 100 % | Gentle | Leaves sub/kick energy intact |
| **Vocal Subtle** | 1 | 10 dB | 50 % | 60 % | Strong | Parallel-style, half-wet ducking |
| **Delta Check** | 1 | 24 dB | 50 % | 100 % | Extreme | Delta on; listen to what is removed |

Bus 1 must match the SC Input's "To Bus"; all presets use Bus 1.

## How it works

### 1. Analysis (SC Input)

A short-time Fourier transform with 1024 points, hop 256 (75 % overlap) and a
square-root Hann window. Each frame produces:

- **chroma[12]** — pitch class energy
- **notes[128]** — note probabilities, smoothed over frames with a floor, so
  the mask cannot flicker
- **spectrum[128]** — 128 log-spaced bands, stored **relative to the loudest
  band** in dB (absolute dB would make every window sidelobe look like signal)
- **level** — frame RMS, used as a silence gate

A frame is published once per hop with a `CLOCK_MONOTONIC` timestamp.

### 2. Bus

`/dev/shm/qdssd_sc_buses_v1`, eight buses, 64 frame slots each, protected by a
seqlock. The reader takes the newest complete frame. The pool is opened lazily
in `process()`, so plugin load order does not matter. Without a sender the
receiver is transparent.

### 3. Mask (QDSSD)

The key frame is turned into 16 band weights in three steps:

- **Note mask** — for every active note (fundamental `f0`, confidence `c`),
  a harmonic comb: `w(h) = 1/h^0.8` at `f = h·f0`, bandwidth about a quarter
  tone.
- **Spectrum mask** — `v = clamp((dB_rel + 45) / 45, 0, 1)` per band.
- **Hybrid** — the maximum of both.

Then:

- **neighbour smearing** — a ducked band pulls its neighbours down (80 % for
  ±1 band, ~48 % for ±2)
- **broadband floor** — above the sub band, every band gets a level-independent
  floor (profile dependent)
- **profile curve** — `w^curve`
- **sub protection** — bands 0 and 1 are scaled to 30 %, so the kick/808 stays

### 4. Filterbank (zero latency)

16 logarithmic bands from 60 Hz to 16 kHz. Each split is a second-order
Butterworth lowpass with `HP = x − LP`; because LP + HP = x exactly, the tree
reconstructs the input perfectly when every gain is 1. The band gain is

```
g_b = 1 - depthLin * w_b        depthLin = 1 - 10^(-Depth/20)
```

smoothed per sample with an attack/release one-pole. There is **no FFT in the
audio path**, so the latency is **0 samples** — no plugin delay compensation
is needed and the beat stays in time.

### Intensity profiles

| Profile | Depth scale | Broadband floor | Curve | Effect at Depth 24 |
|---|---|---|---|---|
| Gentle | 0.50 | 15 % | 1.00 | ~−1 dB overall, clear notes only |
| Strong | 0.80 | 40 % | 0.80 | ~−4 dB overall |
| Extreme | 1.00 | 70 % | 0.65 | ~−9 dB overall, broad carve |

## Mathematics in brief

```
DFT            X[k] = sum_{n=0}^{N-1} x[n] e^(-i 2 pi k n / N)
bin frequency  f_k = k * fs / N
resolution     df = fs / N            (1024 @ 44.1 kHz -> 43.07 Hz)
frame length   N / fs = 23.2 ms       hop = N/4 = 5.8 ms
window         w[n] = sqrt(0.5 * (1 - cos(2 pi n / N)))
COLA           sum of Hann at hop N/4 = 2   -> OLA norm = 0.5
dB             L = 20 log10(A / A0)
MIDI note      n = 69 + 12 log2(f / 440)
quarter tone   ratio 2^(1/24) = 1.0293
band edges     e_i = 60 * (16000/60)^(i/16)
depth          g = 1 - (1 - 10^(-Depth/20)) * w
one-pole       alpha = 1 - exp(-1 / (t * fs))
```

Worked example: a vocal at A3 = 220 Hz (MIDI 57). 220 Hz lies between band
edges 171 Hz and 243 Hz, so band 3 is ducked. With Depth 24 and weight 1 the
gain is `1 − 0.9369 = 0.0631`, i.e. **−24 dB**. The sub band at 60 Hz is not
touched. The same vocal also puts harmonics at 440, 660, 880 Hz, which pull
bands 5–7 down.

## Building

Requirements: Docker, a C++17 compiler for the native tests, Python 3.

```bash
./scripts/build-mpc.sh      # host tests, skins, ARM32/ARM64, validation, ZIP
./scripts/install-mpc.sh    # copies both folders to a connected MPC
```

Output:

```
dist/mpc/r3d0xStyle - VST - QDSSD/
dist/mpc/r3d0xStyle - VST - QDSSD SC Input/
dist/r3d0xStyle - VST - QDSSD-1.1.3.zip
```

## Tests

```
mpc/tools/spec-test.cpp    STFT engine: unity at 0 dB, latency, ducking
mpc/tools/duck-test.cpp    filterbank: exact reconstruction, full duck
mpc/tools/bus-test.cpp     shared memory: seqlock, timestamps, 8 buses
mpc/tools/chain-test.cpp   analysis -> mask -> filterbank, all profiles
```

Latest results:

```
spec-test    PASS   (unity at Depth 0, -7.1 dB at 12 dB, latency exactly N)
duck-test    PASS   (flatness < 1e-5, full duck -149 dB)
bus-test     PASS   (8 checks, 1000 frames round-trip)
chain-test   PASS   Gentle -2.9 dB / Strong -6.0 dB / Extreme -6.3 dB at 220 Hz
host sim     19/19 and 20/20 PASS
skin check   0 errors, 0 warnings
```

## Measurements

Exports of a real project (19.46 s, 44.1 kHz, 24 bit) show:

- **0 samples of delay**, correlation 0.994 with the dry mix
- no clicks, no clipping
- the **sub is preserved**: the removed signal is −15 dB relative to the mix
  at 31 Hz
- from **250 Hz upwards the removal is substantial** — the delta reaches the
  level of the mix, i.e. the wet content is almost fully removed there

Caveat: the old test exports were peak-limited by the previous master chain
at −0.09 dBFS; the MPC itself does not normalise. Absolute level comparisons
between those files are not reliable. Band ratios inside one file are. See
[`ANALYSE.md`](ANALYSE.md) for the full report.

## Limitations and status

- **Prototype, version 1.1.3.** The DSP is tested; the product polish is not.
- **Note detection is deliberately simple** (chroma accumulation + harmonic
  product spectrum). Breath and polyphonic material smear; the mask is soft
  and Hybrid is the default.
- **No desktop build yet.** In a DAW a real 4-input plugin would be possible.
- The MPC stores plugin state in its own opaque binary format, so parameters
  cannot be edited offline in a `.xpj`.

Next steps: unnormalised exports for exact dB measurements, a better note
tracker (YIN + hysteresis), and a desktop build with a real spectrum analyser.

## Repository layout

```
src/dsp/FilterDucker.hpp    16-band filterbank, zero latency
src/dsp/KeyMask.hpp         notes/spectrum -> band weights, intensity profiles
src/dsp/NoteAnalyzer.hpp    vocal STFT analysis
src/dsp/Bus.hpp             shared-memory bus with seqlock
src/dsp/SpectralEngine.hpp  STFT resonance engine (currently unused)
src/QDssdMpc.cpp            receiver plugin
src/QDssdScInput.cpp        sender plugin
mpc/                        skins, metadata, tools, MPC framework
scripts/                    build and install
docker/                     ARM cross-compile image
concept0..2, *.webp         original design notes and concept graphics
HANDOVER.md                 technical handover (German)
ANALYSE.md                  measurement report (German)
```

## License and credits

QDSSD is **GPL-3.0-or-later**; see [`LICENSE`](LICENSE).
The MPC Plugin SDK framework is MIT licensed; see
[`mpc/framework/MPC-PLUGIN-SDK-LICENSE`](mpc/framework/MPC-PLUGIN-SDK-LICENSE).
The design notes in `concept0..2` were drafted with Gemini and revised here.
