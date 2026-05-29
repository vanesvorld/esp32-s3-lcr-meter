#include <Arduino.h>
#include <LiquidCrystal.h>
#include "SineGenerator.h"
#include "AdcSampler.h"
#include "LcrMath.h"

#define PWM_CHANNEL 0
#define PWM_PIN     4
#define PWM_FREQ    100000
#define PWM_RES     8

// --- Initializare Ecran LCD1602 (4-bit mode) ---
// RS=9, E=10, D4=11, D5=12, D6=13, D7=14
LiquidCrystal lcd(9, 10, 11, 12, 13, 14);

// Buffere locale in care copiem datele din AdcSampler pentru procesare
static int vinLocal[ADC_BUF_SIZE];
static int voutLocal[ADC_BUF_SIZE];

// Rezultatul LCR calculat
static LcrResult lcrResult;
static int diagCount = 0;

// --- Functie pentru afisarea pe LCD1602 ---
// --- Functie pentru afisarea pe LCD1602 OPTIMIZATA ---
void updateLCD(const LcrResult& r) {
  // ELIMINAT: lcd.clear() pentru a nu bloca procesorul timp de 1.5ms!
  
  if (!r.valid) {
    lcd.setCursor(0, 0);
    lcd.print("LCR Meter Ready "); // Spatiile de la final sterg caracterele vechi
    lcd.setCursor(0, 1);
    lcd.print("Insert Component");
    return;
  }

  // Linia 1: Afisarea tipului componentei
  lcd.setCursor(0, 0);
  lcd.print("Type: ");
  if (r.type == LCR_RESISTOR) {
    lcd.print("Resistor  "); // Pad cu spatii pt a acoperi cuvinte mai lungi
  } else if (r.type == LCR_CAPACITOR) {
    lcd.print("Capacitor ");
  } else if (r.type == LCR_INDUCTOR) {
    lcd.print("Inductor  ");
  } else {
    lcd.print("Unknown   ");
  }

  // Linia 2: Afisarea valorii
  lcd.setCursor(0, 1);
  lcd.print("Val: ");

  char valStr[16];
  if (r.type == LCR_CAPACITOR) {
      if (r.value < 1e-6f)      snprintf(valStr, sizeof(valStr), "%.1f pF       ", r.value * 1e12f);
      else if (r.value < 1e-3f) snprintf(valStr, sizeof(valStr), "%.1f nF       ", r.value * 1e9f);
      else                      snprintf(valStr, sizeof(valStr), "%.2f uF       ", r.value * 1e6f);
  } else if (r.type == LCR_INDUCTOR) {
      if (r.value < 1e-3f)      snprintf(valStr, sizeof(valStr), "%.1f uH       ", r.value * 1e6f);
      else if (r.value < 1.0f)  snprintf(valStr, sizeof(valStr), "%.1f mH       ", r.value * 1e3f);
      else                      snprintf(valStr, sizeof(valStr), "%.2f H        ", r.value);
  } else {
      if (r.value < 1e3f)       snprintf(valStr, sizeof(valStr), "%.1f Ohm      ", r.value);
      else                      snprintf(valStr, sizeof(valStr), "%.2f kOhm     ", r.value / 1e3f);
  }
  
  lcd.print(valStr);
}

void setup() {
  Serial.begin(921600);
  while (!Serial) delay(10);
  Serial.println("-Serial initialized");

  // Initializare LCD 16 coloane x 2 linii
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("Starting System");
  lcd.setCursor(0, 1);
  lcd.print("Please wait...");

  if (generateSineWave(PWM_PIN, PWM_FREQ, PWM_RES, PWM_CHANNEL)) {
    Serial.println("-PWM generation started on pin " + String(PWM_PIN) +
                   " at " + String(PWM_FREQ) + " Hz, " + String(PWM_RES) + " bits");
  } else {
    Serial.println("-ERROR: Failed to generate sine wave");
  }

  adcSamplerInit();
  Serial.println("-ADC sampler initialized (10kHz, buf=" + String(ADC_BUF_SIZE) + ")");

  // Self-test cu date sintetice (ramane pe Serial pentru debug)
  lcrSelfTest();

  Serial.println("\n--- LCR Meter ready ---");
}

void loop() {
  int readyBuf = adcGetReadyBufIndex();
  if (readyBuf == -1) return;

  adcGetBuffer(readyBuf, vinLocal, voutLocal);
  adcReleaseBuffer(readyBuf);

  // Proceseaza datele brute prin LcrMath
  lcrProcess(vinLocal, voutLocal, ADC_BUF_SIZE, lcrResult);

  // Actualizam ecranul LCD doar o data la 5 buffere 
  // (pentru a evita fenomenul de "flickering" extrem pe cristalele lichide)
  diagCount++;
  if (diagCount % 10 == 0) {
    updateLCD(lcrResult);
  }
}