#include <Arduino.h>
#include "SineGenerator.h"

// Esantioane pe perioada de sinus. Vezi nota despre timer mai jos: pe ESP32
// clasic (T-Display) ledcWrite() in ISR costa ~13 µs, deci ISR-ul nu poate
// rula la 10 µs (100 kHz). Folosim 50 de esantioane la 20 µs => 50*20 = 1000 µs
// = 1 kHz exact, cu marja confortabila peste costul ledcWrite.
#define SAMPLES         50
#define SINE_STEP_US    20      // perioada alarmei timerului (50 kHz ISR)


int sinTable[SAMPLES];
volatile int sampleIndex = 0; 

hw_timer_t *timer = NULL;

// valoare provizorie inaite de a primi canalul ca parametru
uint8_t activePwmChannel = 0;

// punem in IRAM functia de intrerupere ca sa fie rapida
void IRAM_ATTR onTimer(){  
  ledcWrite(activePwmChannel, sinTable[sampleIndex]);
  sampleIndex++;
  
  if (sampleIndex >= SAMPLES){
    sampleIndex = 0;
  }
}

bool generateSineWave(uint8_t pin, uint32_t freq, uint8_t resolution, uint8_t channel) {
  

  // input validation starting

  if (resolution < 1 || resolution > 16) {
    return false;
  }
  
  // ESP32-S3 low-speed LEDC uses 40 MHz XTAL clock
  // Max frequency = clock_freq / (2^resolution)
  const uint32_t LEDC_CLOCK_FREQ = 40000000;
  uint32_t maxFreq = LEDC_CLOCK_FREQ / (1 << resolution);
  
  if (freq > maxFreq) {
    return false;
  }
  
  if (freq == 0) {
    return false;
  }
  
  // input validation ending

  activePwmChannel = channel;

  for (int i = 0; i < SAMPLES; i++) {
    float angle = (2.0 * PI * i) / SAMPLES;  // [0, 2*PI] normat la sample ca sa fie intre 0-255
    sinTable[i] = (int)(127.5 + 90 * sin(angle)); // intre 20% - 80% [51,204] amplitudine sinus
  }

  ledcSetup(channel, freq, resolution);
  ledcAttachPin(pin, channel);

  
  //-----------------------------------
  // -------TIMER INITIALIZATION-------
  //-----------------------------------
  // semnal 1 kHz (1000 perioade pe secunda)
  // => esantionam in SAMPLES (50) puncte
  // => un esantion la fiecare SINE_STEP_US (20 µs) => 50*20µs = 1000µs = 1 kHz
  //
  // NOTA (ESP32 clasic vs S3): la 10 µs/esantion (100 pct) ISR-ul NU se incadra
  // pe ESP32 clasic — ledcWrite() costa ~13 µs, deci ISR-ul rula liber la ~13.2
  // µs si sinusul iesea la ~759 Hz (cadea in bin-ul gresit al DFT). La 20 µs HW
  // timer-ul dicteaza ritmul (ISR-ul se incadra lejer) si sinusul e exact 1 kHz.
  //-----------------------------------


  // ceasul default este de 80 MHz -- divider 80 ca sa ramana 1 MHz (1 µs/tick)
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, SINE_STEP_US, true);
  timerAlarmEnable(timer);
  
  return true;
}