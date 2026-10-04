#include <Arduino.h>
#include <esp_system.h>
#include <TFT_eSPI.h>
#include "SineGenerator.h"
#include "AdcSampler.h"
#include "LcrMath.h"

// PWM carrier output. GPIO4 (used on the TinyS3 fork) is the T-Display's TFT
// backlight, so on this board the stimulus moves to a free header pin.
#define PWM_CHANNEL 0
#define PWM_PIN     26
#define PWM_FREQ    100000
#define PWM_RES     8

// --- Ecran TFT ST7789 (built into the LilyGo T-Display) ---
// Driver, 135x240 panel and the fixed on-board SPI pins (MOSI=19, SCLK=18,
// CS=5, DC=16, RST=23, backlight=4) are all configured via build_flags in
// platformio.ini, so there is nothing to wire and nothing to set here. We use
// rotation 1 (landscape) → the usable canvas is 240x135.
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 135

TFT_eSPI tft = TFT_eSPI();
// Off-screen back-buffer: we redraw the whole readout every ADC frame (~8 fps),
// so we render into a sprite and push it in one shot to avoid flicker.
TFT_eSprite canvas = TFT_eSprite(&tft);

// Buffere locale in care copiem datele din AdcSampler pentru procesare
static int vinLocal[ADC_BUF_SIZE];
static int voutLocal[ADC_BUF_SIZE];

// Rezultatul LCR calculat
static LcrResult lcrResult;

// --- Afisare "live" pe TFT ST7789 (240x135) ---
// Apelat la fiecare cadru ADC (~120ms) pentru un flux continuu. Afiseaza mereu
// |Z| si faza (ca sa se vada ca masoara in timp real); tipul si valoarea R/L/C
// apar cand cadrul e valid, altfel "--". Desenam in sprite si il impingem
// dintr-o singura data ca sa nu palpaie ecranul.
void updateDisplay(const LcrResult& r) {
  canvas.fillSprite(TFT_BLACK);

  // Linia 1: tipul componentei (verde cand e valid, gri cand nu)
  const char* typeStr = "--";
  uint16_t typeColor = TFT_DARKGREY;
  if (r.valid && r.type == LCR_RESISTOR)       { typeStr = "Resistor";  typeColor = TFT_GREEN; }
  else if (r.valid && r.type == LCR_CAPACITOR) { typeStr = "Capacitor"; typeColor = TFT_CYAN;  }
  else if (r.valid && r.type == LCR_INDUCTOR)  { typeStr = "Inductor";  typeColor = TFT_YELLOW; }

  canvas.setTextSize(2);
  canvas.setTextColor(TFT_DARKGREY, TFT_BLACK);
  canvas.setCursor(0, 2);
  canvas.print("Type: ");
  canvas.setTextColor(typeColor, TFT_BLACK);
  canvas.print(typeStr);

  // Linia 2: valoarea cu font mare
  char valStr[16];
  if (!r.valid) {
      snprintf(valStr, sizeof(valStr), "--");
  } else if (r.type == LCR_CAPACITOR) {
      if (r.value < 1e-6f)      snprintf(valStr, sizeof(valStr), "%.1f pF", r.value * 1e12f);
      else if (r.value < 1e-3f) snprintf(valStr, sizeof(valStr), "%.1f nF", r.value * 1e9f);
      else                      snprintf(valStr, sizeof(valStr), "%.2f uF", r.value * 1e6f);
  } else if (r.type == LCR_INDUCTOR) {
      if (r.value < 1e-3f)      snprintf(valStr, sizeof(valStr), "%.1f uH", r.value * 1e6f);
      else if (r.value < 1.0f)  snprintf(valStr, sizeof(valStr), "%.1f mH", r.value * 1e3f);
      else                      snprintf(valStr, sizeof(valStr), "%.2f H", r.value);
  } else if (r.type == LCR_RESISTOR) {
      if (r.value < 1e3f)       snprintf(valStr, sizeof(valStr), "%.1f Ohm", r.value);
      else                      snprintf(valStr, sizeof(valStr), "%.2f kOhm", r.value / 1e3f);
  } else {
      snprintf(valStr, sizeof(valStr), "--");
  }
  canvas.setTextSize(4);
  canvas.setTextColor(r.valid ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
  canvas.setCursor(0, 34);
  canvas.print(valStr);

  // Linia 3-4: streaming |Z| si faza (mereu afisate)
  canvas.setTextSize(2);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
  char zStr[22];
  if (r.magnitude < 1e3f) snprintf(zStr, sizeof(zStr), "|Z|=%.1f Ohm", r.magnitude);
  else                    snprintf(zStr, sizeof(zStr), "|Z|=%.2f kOhm", r.magnitude / 1e3f);
  canvas.setCursor(0, 88);
  canvas.print(zStr);

  char pStr[22];
  snprintf(pStr, sizeof(pStr), "ph=%+.1f deg", r.phaseDeg);
  canvas.setCursor(0, 112);
  canvas.print(pStr);

  canvas.pushSprite(0, 0);
}

// Motivul ultimului reset (diagnostic pentru boot-loop).
static const char* resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "PowerOn";
    case ESP_RST_EXT:       return "ExtPin";
    case ESP_RST_SW:        return "SwReset";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "IntWDT";
    case ESP_RST_TASK_WDT:  return "TaskWDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DeepSleep";
    default:                return "Unknown";
  }
}

void setup() {
  Serial.begin(921600);
  // Timeout, ca sa nu blocheze boot-ul daca nu e atasat un host USB.
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 1500) delay(10);
  Serial.println("-Serial initialized");

  // Motivul ultimului reset: cheia pentru diagnosticarea boot-loop-ului.
  const char* rr = resetReasonStr();
  Serial.printf("-Last reset reason: %s\n", rr);

  // Initializare TFT ST7789 (built-in). Rotatie 1 = landscape 240x135.
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // Alocam sprite-ul back-buffer (240x135 @ 16bpp ≈ 63 KB).
  canvas.setColorDepth(16);
  if (!canvas.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT)) {
    Serial.println("-ERROR: failed to allocate TFT sprite");
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("Starting System");
  tft.print("Last reset: ");
  tft.println(rr);
  delay(1200);  // lasa mesajul vizibil pe ecran

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
  if (readyBuf == -1) {
    // Niciun cadru gata inca. Cedam CPU-ul: fara asta loopTask face busy-spin
    // pe Core 1, task-ul idle nu mai ruleaza si Task Watchdog-ul da reset (~5s).
    // Cadrele vin la ~120ms, deci 1ms latenta e neglijabila.
    delay(1);
    return;
  }

  adcGetBuffer(readyBuf, vinLocal, voutLocal);
  adcReleaseBuffer(readyBuf);

  // Proceseaza datele brute prin LcrMath
  lcrProcess(vinLocal, voutLocal, ADC_BUF_SIZE, lcrResult);

  // Stream "live": actualizam ecranul la fiecare cadru (~120ms, ~8 fps).
  // Bufferul e deja eliberat mai sus, deci esantionarea continua pe Core 0
  // in timp ce desenam (~20ms I2C) pe Core 1.
  updateDisplay(lcrResult);
}