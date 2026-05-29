#include "LcrMath.h"
#include "AdcSampler.h"
#include <math.h>
#include <string.h>

// ─── Configurari Filtru Digital ────────────────────────────
#define MEDIAN_WINDOW 15  // Numarul total de esantioane din istoric
#define BEST_SAMPLES 7    // CATE esantioane cu faza buna pastram (recomandat IMPAR)

// ─── Internal state ────────────────────────────────────────
static float s_phaseCorrection = PHASE_CORRECTION_DEG;
static float s_skewCompensationDeg = 0.0f;

void lcrSetPhaseCorrection(float deg) {
    s_phaseCorrection = deg;
}

// ─── DC removal (subtract mean) ────────────────────────────
static float removeDc(const int* raw, float* centered, int n) {
    long sum = 0;
    for (int i = 0; i < n; i++) sum += raw[i];
    float mean = (float)sum / n;
    for (int i = 0; i < n; i++) centered[i] = raw[i] - mean;
    return mean;
}

// ─── Goertzel (single bin DFT) ─────────────────────────────
static void goertzel(const float* data, int N, int k,
                     float& mag, float& phaseRad) {
    float sPrev = 0.0f;
    float sPrev2 = 0.0f;
    float coeff = 2.0f * cosf(2.0f * PI * k / N);

    for (int i = 0; i < N; i++) {
        float s = data[i] + coeff * sPrev - sPrev2;
        sPrev2 = sPrev;
        sPrev  = s;
    }

    float real = sPrev - sPrev2 * cosf(2.0f * PI * k / N);
    float imag = sPrev2 * sinf(2.0f * PI * k / N);

    mag   = sqrtf(real * real + imag * imag) * 2.0f / N;   
    phaseRad = atan2f(imag, real);
}

// ─── Main processing function ──────────────────────────────
void lcrProcess(const int* vinRaw, const int* voutRaw, int n,
                LcrResult& result) {
    result.type    = LCR_UNKNOWN;
    result.typeStr[0] = '?';
    result.typeStr[1] = '\0';
    result.value     = 0.0f;
    result.magnitude = 0.0f;
    result.phaseDeg  = 0.0f;
    result.valid     = false;

    if (n < 10) return;   

    float* vin   = (float*)malloc(n * sizeof(float));
    float* vout  = (float*)malloc(n * sizeof(float));
    if (!vin || !vout) {
        if(vin) free(vin);
        if(vout) free(vout);
        return;   
    }
    removeDc(vinRaw,  vin,  n);
    removeDc(voutRaw, vout, n);

    int k = (int)(n * TARGET_FREQ / SAMPLE_RATE + 0.5f);
    
    float magVin, phaseVinRad;
    float magVout, phaseVoutRad;

    goertzel(vin,  n, k, magVin,  phaseVinRad);
    goertzel(vout, n, k, magVout, phaseVoutRad);

    free(vin);
    free(vout);

    // --- GUARD 1: Verifica Alimentarea Hardware ---
    if (magVin < 40.0f) {
        result.valid = false;
        return; 
    }

    float vinReRaw = magVin * cosf(phaseVinRad);
    float vinImRaw = magVin * sinf(phaseVinRad);
    
    float voutReRaw = magVout * cosf(phaseVoutRad);
    float voutImRaw = magVout * sinf(phaseVoutRad);

    float skewRad = 2.0f * PI * ADC_CHANNEL_SKEW_US * TARGET_FREQ / 1e6f;
    float cosSkew = cosf(skewRad);
    float sinSkew = sinf(skewRad); 

    float voutReCorr = voutReRaw * cosSkew + voutImRaw * sinSkew;
    float voutImCorr = voutImRaw * cosSkew - voutReRaw * sinSkew;

    float numRe = R_REF_OHMS * voutReCorr;
    float numIm = R_REF_OHMS * voutImCorr;

    float denRe = vinReRaw - voutReCorr;
    float denIm = vinImRaw - voutImCorr;

    float denMagSq = denRe * denRe + denIm * denIm;
    if (denMagSq < 1e-12f) return;

    float zRe = (numRe * denRe + numIm * denIm) / denMagSq;
    float zIm = (numIm * denRe - numRe * denIm) / denMagSq;

    result.magnitude = sqrtf(zRe * zRe + zIm * zIm);
    float phaseRaw   = atan2f(zIm, zRe) * 180.0f / PI;

    result.phaseDeg = phaseRaw + s_phaseCorrection; 

    while (result.phaseDeg > 180.0f)  result.phaseDeg -= 360.0f;
    while (result.phaseDeg < -180.0f) result.phaseDeg += 360.0f;

    // ─── Filtru Inteligent Hibrid Adaptiv (Tinta Dinamica) ────────────
    static float magBuffer[MEDIAN_WINDOW];
    static float phaBuffer[MEDIAN_WINDOW];
    static int histIdx = 0;
    static bool histFilled = false;

    magBuffer[histIdx] = result.magnitude;
    phaBuffer[histIdx] = result.phaseDeg;
    histIdx++;
    if (histIdx >= MEDIAN_WINDOW) {
        histIdx = 0;
        histFilled = true;
    }

    int count = histFilled ? MEDIAN_WINDOW : (histIdx > 0 ? histIdx : 1);
    
    float phaSorted[MEDIAN_WINDOW];
    for (int i = 0; i < count; i++) phaSorted[i] = phaBuffer[i];
    
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (phaSorted[i] > phaSorted[j]) {
                float t = phaSorted[i]; phaSorted[i] = phaSorted[j]; phaSorted[j] = t;
            }
        }
    }
    float medianPhase = phaSorted[count / 2];
    float targetPhase = medianPhase; 

    struct Sample { float mag; float pha; float dev; };
    Sample samples[MEDIAN_WINDOW];
    
    for (int i = 0; i < count; i++) {
        samples[i].mag = magBuffer[i];
        samples[i].pha = phaBuffer[i];
        samples[i].dev = fabsf(phaBuffer[i] - targetPhase);
    }

    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (samples[i].dev > samples[j].dev) {
                Sample t = samples[i]; samples[i] = samples[j]; samples[j] = t;
            }
        }
    }

    int bestK = (count < BEST_SAMPLES) ? count : BEST_SAMPLES;
    float bestMags[BEST_SAMPLES];
    float sumPha = 0;
    
    for (int i = 0; i < bestK; i++) {
        bestMags[i] = samples[i].mag; 
        sumPha += samples[i].pha;     
    }

    for (int i = 0; i < bestK - 1; i++) {
        for (int j = i + 1; j < bestK; j++) {
            if (bestMags[i] > bestMags[j]) {
                float t = bestMags[i]; bestMags[i] = bestMags[j]; bestMags[j] = t;
            }
        }
    }

    result.magnitude = bestMags[bestK / 2]; 
    result.phaseDeg  = sumPha / bestK;
    // ────────────────────────────────────────────────────────

    // -- 7. Clasificare si Calibrare pe tip de componenta --
    float absPhase = fabsf(result.phaseDeg);

    // --- GUARD: Detectie "Fara Componenta" sau "Aparat Oprit" ---
    if (absPhase > 110.0f || result.magnitude > 14000.0f) {
        result.type = LCR_UNKNOWN;
        strcpy(result.typeStr, "?");
        result.value = 0.0f;
        result.valid = false;
        return; 
    }

    const float CALIB_FACTOR_R = 1.09f; 
    const float CALIB_FACTOR_C = 1.00f; 
    const float CALIB_FACTOR_L = 1.00f; 

    if (absPhase < 30.0f) {
        result.type = LCR_RESISTOR;
        strcpy(result.typeStr, "R");
        result.magnitude = result.magnitude * CALIB_FACTOR_R; 
        result.value = result.magnitude;
    } else if (result.phaseDeg < -30.0f) {
        result.type = LCR_CAPACITOR;
        strcpy(result.typeStr, "C");
        result.magnitude = result.magnitude * CALIB_FACTOR_C;
        float f = (k * SAMPLE_RATE) / n;
        result.value = 1.0f / (2.0f * PI * f * result.magnitude);
    } else if (result.phaseDeg > 30.0f) {
        result.type = LCR_INDUCTOR;
        strcpy(result.typeStr, "L");
        result.magnitude = result.magnitude * CALIB_FACTOR_L;
        float f = (k * SAMPLE_RATE) / n;
        result.value = result.magnitude / (2.0f * PI * f);
    } else {
        strcpy(result.typeStr, "?");
    }

    result.valid = (result.type != LCR_UNKNOWN);
}

// ─── Self-test cu date sintetice ───────────────────────
int lcrSelfTest() {
    const int N = 200;            
    const float fs = SAMPLE_RATE; 

    int k = (int)(N * TARGET_FREQ / fs + 0.5f);
    int failures = 0;

    Serial.println("\n===== LCR SELF-TEST =====");

    {
        int vin[N], vout[N];
        float amplitude = 1000.0f;
        float skewSec = 50e-6f; 

        for (int i = 0; i < N; i++) {
            float tVin  = (float)i / fs;
            float tVout = tVin + skewSec;
            vin[i]  = (int)(amplitude * sinf(2.0f * PI * TARGET_FREQ * tVin)) + 2048;
            vout[i] = (int)((amplitude / 2.0f) * sinf(2.0f * PI * TARGET_FREQ * tVout)) + 2048;
        }

        LcrResult res;
        for(int m=0; m<MEDIAN_WINDOW; m++) lcrProcess(vin, vout, N, res); 

        Serial.printf("Test R: |Z|=%.2fΩ phase=%.1f° -> %s",
                      res.magnitude, res.phaseDeg, res.typeStr);
        bool ok = (res.type == LCR_RESISTOR)
               && (fabsf(res.magnitude - R_REF_OHMS) < R_REF_OHMS * 0.15f);
        Serial.println(ok ? " PASS" : " FAIL");
        if (!ok) failures++;
    }

    {
        int vin[N], vout[N];
        float amplitude = 1000.0f;
        float C = 1e-6f;
        float capZ = 1.0f / (2.0f * PI * TARGET_FREQ * C); 
        float Xc = 1.0f / (2.0f * PI * TARGET_FREQ * C); 
        
        float denSq = R_REF_OHMS * R_REF_OHMS + Xc * Xc;
        float vRatioRe = (Xc * Xc) / denSq;
        float vRatioIm = (-R_REF_OHMS * Xc) / denSq;
        
        float vMag = amplitude * sqrtf(vRatioRe * vRatioRe + vRatioIm * vRatioIm);
        float vPhase = atan2f(vRatioIm, vRatioRe);

        float skewSec = 50e-6f; 
        for (int i = 0; i < N; i++) {
            float tVin  = (float)i / fs;
            float tVout = tVin + skewSec;
            vin[i]  = (int)(amplitude * sinf(2.0f * PI * TARGET_FREQ * tVin)) + 2048;
            vout[i] = (int)(vMag * sinf(2.0f * PI * TARGET_FREQ * tVout + vPhase)) + 2048;
        }

        LcrResult res;
        for(int m=0; m<MEDIAN_WINDOW; m++) lcrProcess(vin, vout, N, res);

        Serial.printf("Test C: |Z|=%.2fΩ phase=%.1f° -> %s value=%.2e",
                      res.magnitude, res.phaseDeg, res.typeStr, res.value);

        bool ok = (res.type == LCR_CAPACITOR)
               && (fabsf(res.magnitude - capZ) < capZ * 0.15f);
        Serial.println(ok ? " PASS" : " FAIL");
        if (!ok) failures++;
    }

    {
        int vin[N], vout[N];
        float amplitude = 1000.0f;
        float L = 0.01f;
        float indZ = 2.0f * PI * TARGET_FREQ * L; 
        float vRatioRe = indZ * indZ / (R_REF_OHMS * R_REF_OHMS + indZ * indZ);
        float vRatioIm = R_REF_OHMS * indZ / (R_REF_OHMS * R_REF_OHMS + indZ * indZ);
        float vMag = amplitude * sqrtf(vRatioRe * vRatioRe + vRatioIm * vRatioIm);
        float vPhase = atan2f(vRatioIm, vRatioRe);

        float skewSec = 50e-6f;
        for (int i = 0; i < N; i++) {
            float tVin  = (float)i / fs;
            float tVout = tVin + skewSec;
            vin[i]  = (int)(amplitude * sinf(2.0f * PI * TARGET_FREQ * tVin)) + 2048;
            vout[i] = (int)(vMag * sinf(2.0f * PI * TARGET_FREQ * tVout + vPhase)) + 2048;
        }

        LcrResult res;
        for(int m=0; m<MEDIAN_WINDOW; m++) lcrProcess(vin, vout, N, res);

        Serial.printf("Test L: |Z|=%.2fΩ phase=%.1f° -> %s value=%.2e",
                      res.magnitude, res.phaseDeg, res.typeStr, res.value);

        bool ok = (res.type == LCR_INDUCTOR)
               && (fabsf(res.magnitude - indZ) < indZ * 0.15f);
        Serial.println(ok ? " PASS" : " FAIL");
        if (!ok) failures++;
    }

    Serial.printf("===== SELF-TEST %s (%d failures) =====\n\n",
                  failures == 0 ? "ALL PASS" : "FAILURES DETECTED", failures);

    return failures;
}