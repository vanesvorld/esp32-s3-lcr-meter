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

**Ron by range (CD-series):** negligible for 1 MΩ / 100 kΩ, a few % at 10 kΩ
(calibratable), but 20–60 % *and nonlinear* at 1 kΩ — too much. Plan: analog mux for
the high ranges, **relay (or hardwired default) for 1 kΩ**. Calibrate every range
against a known resistor so the effective `R_ref` absorbs static Ron.

**Power/logic levels:** run the CD40xx at **3.3 V**, not 5 V. At 5 V its logic-HIGH
threshold (~3.5 V) is above the ESP32's 3.3 V output. At 3.3 V the ESP32 drives it
cleanly and the sine (peaks ~2.5 V) still passes within the 0–3.3 V rails; the only
cost is higher Ron, which only hurts the low range anyway.

## Proposed pin mapping (T-Display + CD4052 + LM358)

Concrete starting point for the plan above: a **CD4052** analog mux handles the
10 kΩ / 100 kΩ / 1 MΩ references, and a **relay** switches the 1 kΩ reference
directly (so the low range sees contact resistance, not mux Ron). The analog front
end (GPIO26 PWM, GPIO32 Vin, GPIO33 Vout) is unchanged from the T-Display port; all
new control lines live on the left-side header, which keeps the analog nodes on the
right header.

### ESP32 (T-Display) control lines

| Signal | GPIO | Header | Notes |
|--------|------|--------|-------|
| Mux select **A** (S0, LSB) | **GPIO13** | left | → CD4052 pin 10 |
| Mux select **B** (S1, MSB) | **GPIO22** | left | → CD4052 pin 9 |
| Mux **INH** (inhibit) | **GPIO17** | left | → CD4052 pin 6. HIGH = all channels open |
| **Relay** drive (1 kΩ range) | **GPIO21** | left | → NPN/MOSFET → coil; add flyback diode. HIGH = 1 kΩ in circuit |
| **Range** button | **GPIO0** | on-board (BOOT) | `INPUT_PULLUP`, debounced, cycles ranges. Strapping pin, but fine as a *runtime* input |

All chosen control pins (13, 17, 21, 22) are non-strapping and output-capable.
GPIO0 reuses the board's BOOT button so no extra wiring is needed; GPIO35 is the
board's other button if you prefer a separate "hold = autorange" control.

### CD4052 (run at 3.3 V, single supply)

Only the **X** half (one 4:1 mux) is needed; the Y half is unused.

| CD4052 pin | Connects to |
|------------|-------------|
| VDD (16) | **3V3** (not 5 V — see logic-level note above) |
| VSS (8) | GND |
| VEE (7) | GND (signal stays within 0–3.3 V, so VEE = VSS) |
| A (10) | GPIO13 |
| B (9) | GPIO22 |
| INH (6) | GPIO17 |
| X-common (13) | **Vout node** (LM358 pin 5) |
| X0 (11) | *unused* (1 kΩ is on the relay) — tie to GND or reserve |
| X1 (14) | 10 kΩ reference, bottom end |
| X2 (15) | 100 kΩ reference, bottom end |
| X3 (12) | 1 MΩ reference, bottom end |
| Y0–Y3, Y-common (1,2,4,5,3) | unused — tie to GND |

The **top** end of every reference (10 k / 100 k / 1 M **and** the relay's 1 kΩ) ties
to the common **Vin node** (LM358 pin 1). Non-selected references have their bottom
end floating (mux channel open), so only the selected reference carries divider
current.

**Range selection** (A is the LSB; channel = B·2 + A):

| Range | Relay (GPIO21) | INH (GPIO17) | B (GPIO22) | A (GPIO13) | Active path |
|-------|:--:|:--:|:--:|:--:|-------------|
| 1 kΩ | HIGH | HIGH | x | x | relay: Vin → 1 kΩ → Vout (mux off) |
| 10 kΩ | LOW | LOW | 0 | 1 | mux X1 |
| 100 kΩ | LOW | LOW | 1 | 0 | mux X2 |
| 1 MΩ | LOW | LOW | 1 | 1 | mux X3 |

### LM358 (unchanged topology, now a switchable reference)

Both halves stay unity-gain followers; the former fixed 1 kΩ between pin 1 and pin 5
becomes the switchable reference bank.

| LM358 pin | Role | Connects to |
|-----------|------|-------------|
| 8 (V+) | supply | **5 V** (+ 100 nF decoupling to pin 4) |
| 4 (V−) | supply | GND |
| 3 (A in+) | sine in | RC filter output (from GPIO26 PWM) |
| 1 (A out) | **Vin node** | pin 2, GPIO32 (ADC), all reference tops, relay contact |
| 2 (A in−) | follower fb | pin 1 |
| 5 (B in+) | **Vout node** | CD4052 X-common, relay contact, DUT top |
| 6 (B in−) | follower fb | pin 7 |
| 7 (B out) | buffered Vout | GPIO33 (ADC) |

DUT connects pin 5 ↔ GND. For the **1 MΩ** range, swap the LM358 for a FET/CMOS-input
op-amp (see below) — this pinout is DIP-8 pin-compatible with MCP6002 / TLV9002.

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
  select line(s) / relay accordingly; show the active range on the TFT.
- Later: **autorange** — pick the reference from the current reading.
- Optional quality feature: **open/short/load calibration** to null fixture
  parasitics (as real LCR meters do).

## Related

- See the boot-loop investigation (Task WDT) — must be resolved before adding this.
- The streaming `|Z|` / phase readout on the TFT is useful for watching a range
  settle during calibration.
