# Future work: multi-range reference switching

Notes captured for a later feature — **not yet implemented**. Goal: let the meter
switch between several reference resistors (selected by a button) so it can measure
far outside the single-range `~100 Ω – 10 kΩ` window the fixed 1 kΩ reference gives
today.

## Background

The front end measures `Z_DUT = R_ref · Vout / (Vin − Vout)` with a single
reference resistor (`R_REF_OHMS = 985 Ω` in `include/LcrMath.h`). Accuracy is best
when `R_ref` is comparable to the DUT impedance, so one fixed reference only covers
about two decades. High-value parts (≫ 10 kΩ) collapse the `Vin − Vout` difference
below the ADC's noise floor and read as garbage.

The `|Z| > 14 kΩ` / `|phase| > 110°` "no component" guard in `LcrMath.cpp` has
**already been removed** so high-impedance DUTs are reported instead of rejected —
that was a prerequisite for ranging.

## Target ranges

Decided: **1 kΩ / 10 kΩ / 100 kΩ / 1 MΩ** reference resistors, one active at a time.
Rough coverage per reference (best accuracy near `R_ref`):

| Reference | Good for roughly |
|-----------|------------------|
| 1 kΩ | 100 Ω – 10 kΩ |
| 10 kΩ | 1 kΩ – 100 kΩ |
| 100 kΩ | 10 kΩ – 1 MΩ |
| 1 MΩ | 100 kΩ – 10 MΩ+ |

## Switching hardware

Only **one** reference may be electrically in-circuit at a time (references in
parallel just form their parallel combination), so a switch element is required
between Vin and the Vout node.

The on-resistance (**Ron**) of the switch adds in series with the reference and
carries the divider current, so it matters most on the *small* references:

| Switch option | Verdict |
|---------------|---------|
| **CD4052** (dual 4-ch analog mux, 2 select lines) | Recommended for 100 k / 1 M (and 10 k if calibrated). Covers all 4 refs with 2 GPIO. |
| CD4051 / CD4053 / CD4066 (CD40xx analog switches) | Also work; same Ron caveat. 4066 needs one control line per switch (enable exactly one in firmware). |
| **Reed / signal relay** | Near-zero Ron — best for the **1 kΩ** (and 10 kΩ) range. Needs a driver transistor + flyback diode each. |
| Mechanical rotary/DIP switch | Lowest Ron, but manual (button only tells firmware which range is active). |
| **74HC595** | **Not a switch** (digital push-pull, can't pass the AC signal, can't tri-state individually). Useful only as a GPIO/control expander to drive mux select lines or relay drivers. |

**Ron by range (CD-series):** negligible for 1 MΩ / 100 kΩ, a few % at 10 kΩ
(calibratable), but 20–60 % *and nonlinear* at 1 kΩ — too much. Plan: analog mux for
the high ranges, **relay (or hardwired default) for 1 kΩ**. Calibrate every range
against a known resistor so the effective `R_ref` absorbs static Ron.

**Power/logic levels:** run the CD40xx at **3.3 V**, not 5 V. At 5 V its logic-HIGH
threshold (~3.5 V) is above the ESP32's 3.3 V output. At 3.3 V the ESP32 drives it
cleanly and the sine (peaks ~2.5 V) still passes within the 0–3.3 V rails; the only
cost is higher Ron, which only hurts the low range anyway.

## For the MΩ ranges specifically

- Swap the **LM358** for a **FET/CMOS-input op-amp** (e.g. MCP6002, TLV9002,
  LMC662) — the LM358's input bias current (up to ~250 nA) loads a megohm node and
  shifts the reading.
- **Stray capacitance** at the Vout node (~10 pF ≈ 16 MΩ reactance at 1 kHz) rivals a
  10 MΩ DUT. Use short leads, a guard ring around the high-impedance node, and
  consider a **lower test frequency** for the high-R ranges.

## Firmware changes required

- Make the reference value **runtime-settable**: replace the compile-time
  `R_REF_OHMS` #define with a variable + `lcrSetRefOhms(float)` setter (currently
  used directly in `LcrMath.cpp`).
- A range table: `{ ohms, selectCode, calibFactor }` per range.
- **Button input** (GPIO with pull-up, debounced) that cycles ranges; drive the mux
  select line(s) / relay accordingly; show the active range on the OLED.
- Later: **autorange** — pick the reference from the current reading.
- Optional quality feature: **open/short/load calibration** to null fixture
  parasitics (as real LCR meters do).

## Related

- See the boot-loop investigation (Task WDT) — must be resolved before adding this.
- The streaming `|Z|` / phase readout on the OLED is useful for watching a range
  settle during calibration.
