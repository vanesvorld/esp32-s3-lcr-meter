# ESP32-S3 LCR Meter (TinyS3 + SSD1306 fork)

> **This is a fork** of the original ESP32-S3 LCR Meter, adapted for the hardware
> I had on hand:
> - **Board:** Unexpected Maker **TinyS3** (ESP32-S3) instead of a generic ESP32-S3 DevKitC.
> - **Display:** I²C **SSD1306 OLED (128×64)** instead of the parallel LCD1602.
> - **Filter cap:** **147 nF** (100 nF ∥ 47 nF) instead of a single 150 nF part.
>
> Original project: `<add upstream repo URL here>` — credit to the original author.
> See [Acknowledgements](#acknowledgements).

## Description
A fully functional LCR meter (Inductance, Capacitance, Resistance) built around the
ESP32-S3. The system uses Digital Signal Processing (DSP) to calculate component
values by analyzing the voltage and phase differences across a known reference
resistor and the Device Under Test (DUT).

## Features
- **Custom Sine Wave Generation:** A 100 kHz PWM carrier is filtered into a clean
  1 kHz sine wave stimulus.
- **Precision Sampling:** ADC sampling runs on a dedicated FreeRTOS task pinned to
  Core 0, eliminating OS jitter. Vin and Vout are sampled alternately (20 kHz tick
  → 10 kHz per channel) with a fixed, compensated skew.
- **DSP Architecture:** Implements the Goertzel algorithm for efficient single-bin
  Discrete Fourier Transform (DFT) amplitude and phase extraction.
- **Adaptive Hybrid Filtering:** A custom median filter with a dynamic phase target
  compensates for physical component imperfections (ESR/DCR) and hardware noise.
- **Standalone Operation:** Real-time classification and measurement on an SSD1306
  OLED over I²C.
- **Robust Error Handling:** Automatic hardware-level detection of missing
  components, open circuits, or an unpowered op-amp.

## How it works (signal chain)
1. **GPIO4** emits a 100 kHz PWM whose duty cycle is stepped through a 100-point
   sine table at a 100 kHz interrupt rate → one sine period per 1 ms → **1 kHz**.
2. An **RC low-pass filter** turns the PWM into a smooth 1 kHz sine (still DC-coupled,
   centred around ~1.65 V).
3. **LM358 buffer A** (voltage follower) drives the sine into the voltage divider.
   Its output is the **Vin** node, sampled by **GPIO5**.
4. The divider is `Vin → R_ref → Vout → DUT → GND`. **Vout** is the voltage across
   the DUT.
5. **LM358 buffer B** isolates the Vout node and feeds **GPIO6**.
6. Firmware computes `Z_DUT = R_ref · Vout / (Vin − Vout)` (complex), extracts
   magnitude and phase via Goertzel, classifies R/L/C by phase sign, and shows the
   result on the OLED.

The DSP works on the **ratio** Vout/Vin, so the absolute sine amplitude (and hence
the exact filter-cap value) is not critical — which is why the 147 nF substitution
is harmless.

---

## 1) Parts list

| Qty | Part | Notes |
|----:|------|-------|
| 1 | **Unexpected Maker TinyS3** (ESP32-S3) | Any ESP32-S3 board works; pin numbers below assume the TinyS3. |
| 1 | **SSD1306 OLED, 128×64, I²C** | Addr `0x3C` (7-bit). Modules labelled **`0x78`** are quoting the 8-bit address — same device. |
| 1 | **LM358** dual op-amp (DIP-8) | Two voltage-follower buffers. Powered from **5 V (VBUS)**. |
| 1 | **R_ref — 1 kΩ, 1% metal film** | Reference resistor for the divider. Firmware is calibrated to `985 Ω` (`R_REF_OHMS`); measure yours and update the constant. |
| 1 | **R_filt — ~1 kΩ** | RC low-pass series resistor. See note below. |
| 2 | **Filter cap: 100 nF + 47 nF** | Wired **in parallel** = 147 nF ≈ 150 nF. A single 150 nF also works. |
| 1 | **100 nF** (optional) | Decoupling cap across the LM358 supply pins. |
| — | Breadboard, jumper wires, USB-C cable | |
| — | Test clips / sockets for the DUT | |

> **R_filt note:** the filter cutoff is `fc = 1 / (2π · R_filt · C_filt)`. With
> `C_filt = 147 nF`, a **1 kΩ** resistor gives `fc ≈ 1.06 kHz`. The exact value lives
> in the **original schematic** (I derived the topology and every code-defined value,
> but this resistor is not in the source). Confirm against the upstream schematic and
> adjust if yours differs — a different R_filt just shifts the sine amplitude, which
> the ratio-based DSP tolerates.

---

## 2) GPIO assignments (TinyS3 / ESP32-S3)

| TinyS3 pin | ADC channel | Dir | Connects to |
|------------|-------------|-----|-------------|
| **GPIO4** | — | OUT | 1 kΩ resistor → LM358 pin 3 |
| **GPIO5** | ADC1_CH4 | IN | LM358 pin 1 |
| **GPIO6** | ADC1_CH5 | IN | LM358 pin 7 |
| **GPIO8** | — | I/O | OLED SDA |
| **GPIO9** | — | OUT | OLED SCL |
| **3V3** | — | PWR | OLED VCC |
| **5V** | — | PWR | LM358 pin 8 |
| **GND** | — | PWR | LM358 pin 4, OLED GND |

> On a **classic ESP32** the channel numbers are identical but map to different pins
> (ADC1_CH4 = GPIO32, ADC1_CH5 = GPIO33), and default I²C is SDA=21/SCL=22. Avoid
> GPIO6–11 on the classic chip (SPI flash).

---

## 3) Schematic

Buffers A and B are the two halves of a single LM358 (unity-gain followers).

![LCR front-end schematic](schematic.svg)

Pinout references: [LM358 DIP](lm358-pinout-2263572354.jpg) · [TinyS3](pins_tinys3.jpg)

---

## 4) Wiring (pin-to-pin connection list)

**Resistors / capacitors (each connects two points)**
- **1 kΩ** resistor: TinyS3 `GPIO4` ↔ LM358 `pin 3`
- **147 nF** cap (100 nF ∥ 47 nF): LM358 `pin 3` ↔ `GND`
- **1 kΩ** resistor: LM358 `pin 1` ↔ LM358 `pin 5`
- **DUT**: LM358 `pin 5` ↔ `GND`
- **100 nF** cap: LM358 `pin 8` ↔ LM358 `pin 4`

**Direct wires**
- TinyS3 `GPIO5` ↔ LM358 `pin 1`
- TinyS3 `GPIO6` ↔ LM358 `pin 7`
- LM358 `pin 1` ↔ LM358 `pin 2`
- LM358 `pin 7` ↔ LM358 `pin 6`

**Power**
- TinyS3 `5V` ↔ LM358 `pin 8`
- TinyS3 `GND` ↔ LM358 `pin 4`

**OLED (I²C)**
- TinyS3 `3V3` ↔ OLED `VCC`
- TinyS3 `GND` ↔ OLED `GND`
- TinyS3 `GPIO8` ↔ OLED `SDA`
- TinyS3 `GPIO9` ↔ OLED `SCL`

---

## Reference image (original build)
The original project's circuit screenshot is kept below for reference. Note that
this fork uses an OLED instead of the LCD and a 147 nF filter cap; the schematic and
wiring above reflect **this fork's** build.

<img width="1709" height="595" alt="Original circuit reference" src="https://github.com/user-attachments/assets/4534fb13-19a9-46fb-9595-37cd43ad6302" />

---

## Build & flash (PlatformIO)
```bash
pio run            # build
pio run -t upload  # flash the TinyS3
pio device monitor # serial @ 921600 (self-test + diagnostics)
```

> **Platform is pinned** to `espressif32@6.9.0` (Arduino-ESP32 **2.x**). This code
> uses the legacy `ledcSetup` / `ledcAttachPin` / `timerBegin(num, div, up)` API,
> which was removed in core 3.x (espressif32 7.x). Do not bump the platform without
> porting those calls.

## Calibration
- Measure your reference resistor and set `R_REF_OHMS` in `include/LcrMath.h`.
- Measure a known resistor and adjust `PHASE_CORRECTION_DEG` / `CALIB_FACTOR_*` if
  needed. The serial self-test (`lcrSelfTest()`) validates the DSP against synthetic
  R/L/C signals on boot.

## Troubleshooting
- **Blank OLED:** the module may be labelled `0x78` — that's the 8-bit address; the
  firmware uses the 7-bit `0x3C`. If yours is `0x7A`, set `OLED_ADDR` to `0x3D`.
- **"SSD1306 not found" on serial:** check SDA/SCL (GPIO8/GPIO9) and that VCC is 3V3.
- **Always reads "Insert Component":** `magVin` guard not met — check the LM358 is
  powered from 5 V and the PWM/filter chain reaches Vin.

## Tools used
- **Hardware:** TinyS3 (ESP32-S3), LM358 dual op-amp, SSD1306 128×64 I²C OLED.
- **Software:** Arduino framework with PlatformIO (C/C++); Adafruit SSD1306 + GFX.

## Acknowledgements
This is a hardware-substitution fork of the original ESP32-S3 LCR Meter. All of the
DSP, sampling, and measurement design is the original author's work. Please replace
`<add upstream repo URL here>` above with the upstream repository link when
registering this as a fork.
