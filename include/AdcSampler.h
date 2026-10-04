#pragma once
#include <Arduino.h>

// ─── Config ────────────────────────────────────────────────
// Alternating sampling: timer fires every 50µs (20kHz).
// On each tick we read ONE channel, alternating Vin/Vout.
// So each channel is still sampled at 10kHz.
#define ADC_BUF_SIZE       1200      // 400 samples per channel = 40 perioade
#define ADC_SAMPLE_US      50       // 50µs per ISR tick = 20kHz timer
#define ADC_VIN_PIN        5        // unused
#define ADC_VOUT_PIN       6        // unused

// Decalajul fix intre Vin[i] si Vout[i] (un interval ADC_SAMPLE_US).
// Vin se citeste la tick-ul par, Vout la tick-ul impar → 50µs decalaj.
// Folosit in LcrMath pentru compensarea fazei.
#define ADC_CHANNEL_SKEW_US 50       // 50µs skew = 18° la 1kHz
// ───────────────────────────────────────────────────────────

void adcSamplerInit();

// DEBUG: measured per-channel sample rate (Hz), timed inside the sampler task
// across one frame fill (excludes the inter-frame pause). 0 until first frame.
float adcGetMeasuredRateHz();

// returns index of ready buffer (0 or 1), -1 if neither are ready
int  adcGetReadyBufIndex();

// Copies ADC buffers in specified arrays
// vinOut and voutOut must have at least ADC_BUF_SIZE elements
// NOTE: Due to alternating sampling, samples in vinOut[i] and voutOut[i]
// are NOT simultaneous – there is a fixed ADC_CHANNEL_SKEW_US skew.
void adcGetBuffer(int bufIdx, int* vinOut, int* voutOut);

//Frees buffer after processing. ISR can write in it again
void adcReleaseBuffer(int bufIdx);

// Returns true if the current data is "full frame" (both Vin and Vout buffers filled)
// Can be used to check data consistency
bool adcIsFullFrame();

// Get raw access to the last acquired buffer data for diagnostics
// Returns the buffer index that is currently ready, or -1
// Fills *outVinFirst and *outVoutFirst with the first sample values
int adcPeek(int* outVinFirst, int* outVoutFirst);
