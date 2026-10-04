#include "AdcSampler.h"
#include "driver/adc.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static volatile int  vinBuf[2][ADC_BUF_SIZE];
static volatile int  voutBuf[2][ADC_BUF_SIZE];
static volatile bool bufReady[2]  = {false, false};
static volatile int  activeBuf    = 0;
static volatile int  writeIdx     = 0;      
static volatile bool expectVin    = true;   

static TaskHandle_t adcTaskHandle = NULL;

// DEBUG: rata reala de esantionare per-canal, masurata INTRE primul si ultimul
// esantion al unui cadru (exclude pauza dintre cadre), deci reflecta viteza
// reala a buclei de esantionare, nu cat de repede o goleste loop().
static volatile uint64_t frameStartUs   = 0;
static volatile float    measuredRateHz = 0.0f;

float adcGetMeasuredRateHz() { return measuredRateHz; }

// Task-ul care înlocuiește ISR-ul. Rulează independent.
void adcTask(void* pvParameters) {
    uint64_t next_read = esp_timer_get_time();
    
    while(1) {
        // Dacă bufferul curent e plin și bucla principală (loop) încă nu l-a eliberat
        if (bufReady[activeBuf]) {
            vTaskDelay(pdMS_TO_TICKS(1)); // Dăm un respiro procesorului
            next_read = esp_timer_get_time(); 
            continue;
        }

        uint64_t now = esp_timer_get_time();
        if (now >= next_read) {
            if (expectVin) {
                // DEBUG: marcam startul cadrului la primul esantion Vin.
                if (writeIdx == 0) frameStartUs = now;
                vinBuf[activeBuf][writeIdx] = adc1_get_raw(ADC1_CHANNEL_4);
                expectVin = false;
            } else {
                voutBuf[activeBuf][writeIdx] = adc1_get_raw(ADC1_CHANNEL_5); 
                expectVin = true;
                
                writeIdx++;
                // Când s-a umplut un cadru complet de date (Vin + Vout)
                if (writeIdx >= ADC_BUF_SIZE) {
                    // DEBUG: rata reala = ADC_BUF_SIZE esantioane / durata cadrului.
                    uint64_t dt = now - frameStartUs;
                    if (frameStartUs != 0 && dt > 0) {
                        measuredRateHz = (float)ADC_BUF_SIZE * 1e6f / (float)dt;
                    }
                    bufReady[activeBuf] = true;
                    activeBuf = 1 - activeBuf;
                    writeIdx  = 0;

                    // FIX PENTRU WATCHDOG:
                    // Punem task-ul pe pauză 1ms între cadre. Această pauză forțată
                    // permite task-ului IDLE de pe Core 0 să ruleze și să reseteze WDT-ul.
                    vTaskDelay(pdMS_TO_TICKS(1)); 
                    next_read = esp_timer_get_time(); // Resincronizăm timerul intern
                    continue;
                }
            }
            next_read += ADC_SAMPLE_US;
        } else {
            taskYIELD(); 
        }
    }
}

void adcSamplerInit() {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_12); // GPIO5
    adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_12); // GPIO6

    // Pornim task-ul pe Core 0, cu prioritate peste idle dar sub interrupts
    xTaskCreatePinnedToCore(
        adcTask, 
        "ADC_Sampler", 
        4096, 
        NULL, 
        2, 
        &adcTaskHandle, 
        0 // Pin to Core 0
    );
}

int adcGetReadyBufIndex() {
    if (bufReady[0]) return 0;
    if (bufReady[1]) return 1;
    return -1;
}

void adcGetBuffer(int bufIdx, int* vinOut, int* voutOut) {
    for (int i = 0; i < ADC_BUF_SIZE; i++) {
        vinOut[i]  = vinBuf[bufIdx][i];
        voutOut[i] = voutBuf[bufIdx][i];
    }
}

void adcReleaseBuffer(int bufIdx) {
    bufReady[bufIdx] = false;
}

int adcPeek(int* outVinFirst, int* outVoutFirst) {
    if (bufReady[0]) {
        if (outVinFirst) *outVinFirst = vinBuf[0][0];
        if (outVoutFirst) *outVoutFirst = voutBuf[0][0];
        return 0;
    }
    if (bufReady[1]) {
        if (outVinFirst) *outVinFirst = vinBuf[1][0];
        if (outVoutFirst) *outVoutFirst = voutBuf[1][0];
        return 1;
    }
    return -1;
}
