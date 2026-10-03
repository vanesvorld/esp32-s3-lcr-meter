#include <Arduino.h>
#include <esp_system.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "SineGenerator.h"
#include "AdcSampler.h"
#include "LcrMath.h"

#define PWM_CHANNEL 0
#define PWM_PIN     4
#define PWM_FREQ    100000
#define PWM_RES     8

// --- Initializare Ecran OLED SSD1306 (I2C) ---
// TinyS3 default I2C pins: SDA=8, SCL=9. OLED does not share pins with the
// PWM output (GPIO4) or the ADC inputs (GPIO5=Vin, GPIO6=Vout).
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1     // shared with the MCU reset / not used on most modules
#define OLED_ADDR     0x3C   // common SSD1306 address (some modules use 0x3D)
#define I2C_SDA       8
#define I2C_SCL       9

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Buffere locale in care copiem datele din AdcSampler pentru procesare
static int vinLocal[ADC_BUF_SIZE];
static int voutLocal[ADC_BUF_SIZE];

// Rezultatul LCR calculat
static LcrResult lcrResult;

// --- Afisare "live" pe OLED SSD1306 ---
// Apelat la fiecare cadru ADC (~120ms) pentru un flux continuu. Afiseaza mereu
// |Z| si faza (ca sa se vada ca masoara in timp real); tipul si valoarea R/L/C
// apar cand cadrul e valid, altfel "--".
void updateDisplay(const LcrResult& r) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Linia 1: tipul componentei
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Type: ");
  if (r.valid && r.type == LCR_RESISTOR)       display.print("Resistor");
  else if (r.valid && r.type == LCR_CAPACITOR) display.print("Capacitor");
  else if (r.valid && r.type == LCR_INDUCTOR)  display.print("Inductor");
  else                                         display.print("--");

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
  display.setTextSize(2);
  display.setCursor(0, 18);
  display.print(valStr);

  // Linia 3-4: streaming |Z| si faza (mereu afisate)
  display.setTextSize(1);
  char zStr[22];
  if (r.magnitude < 1e3f) snprintf(zStr, sizeof(zStr), "|Z|=%.1f Ohm", r.magnitude);
  else                    snprintf(zStr, sizeof(zStr), "|Z|=%.2f kOhm", r.magnitude / 1e3f);
  display.setCursor(0, 46);
  display.print(zStr);

  char pStr[22];
  snprintf(pStr, sizeof(pStr), "ph=%+.1f deg", r.phaseDeg);
  display.setCursor(0, 56);
  display.print(pStr);

  display.display();
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

  // Initializare OLED SSD1306 pe I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("-ERROR: SSD1306 not found at 0x" + String(OLED_ADDR, HEX));
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Starting System");
  display.print("Last reset: ");
  display.println(rr);
  display.display();
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