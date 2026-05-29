#pragma once
#include <Arduino.h>

// ─── Config constants ──────────────────────────────────────
#define R_REF_OHMS          985.0f   // aprox 1kΩ reference resistor
#define SAMPLE_RATE         10000.0f // 10kHz ADC sampling
#define TARGET_FREQ         1000.0f  // 1kHz stimulus
#define PHASE_CORRECTION_DEG   0.0f  // Calibrate empirically with a known resistor!
// ───────────────────────────────────────────────────────────

enum LcrComponentType {
    LCR_UNKNOWN = 0,
    LCR_RESISTOR,
    LCR_CAPACITOR,
    LCR_INDUCTOR
};

struct LcrResult {
    LcrComponentType type;   // R / C / L / unknown
    char typeStr[8];         // printable: "R", "C", "L", "?"
    float value;             // in Ω, F, or H
    float magnitude;         // |Z| in Ω
    float phaseDeg;          // phase in degrees (after correction)
    bool valid;
};

// Process one buffer of raw ADC data (Vin, Vout) and fill LcrResult.
// n = number of samples in each buffer (typically ADC_BUF_SIZE).
void lcrProcess(const int* vin, const int* vout, int n, LcrResult& result);

// Re-set the correction constant at runtime (e.g. from serial calibration).
void lcrSetPhaseCorrection(float deg);

// Self-test: generates synthetic sine waves and runs lcrProcess() on them.
// Returns the number of test failures (0 = all good).
// Prints diagnostics over Serial.
int lcrSelfTest();