#include <SPI.h>
#include <RadioLib.h>
#include "mbedtls/md5.h"

#define LORA_SCK   18
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_CS     5
#define LORA_RST    4
#define LORA_IRQ    2

#define PIN_CH1  34
#define PIN_CH2  35
#define PIN_CH3  32
#define PIN_CH4  33
#define PIN_CH5  25
#define PIN_CH6  26
#define PIN_CH7  27
#define PIN_CH8  14
#define PIN_CH9  12
#define PIN_CH10 13

#define BINDING_PHRASE  "my_drone_link"
#define FHSS_NUM_CH     3
#define FHSS_SEQ_LEN    255
#define SYNC_EVERY      10
#define TX_INTERVAL_MS  10

static const float fhssFreqs[FHSS_NUM_CH] = {433.420f, 433.920f, 434.420f};

SX1278 lora = new Module(LORA_CS, LORA_IRQ, LORA_RST);

uint8_t uid[6];
uint8_t fhssSeq[FHSS_SEQ_LEN];
uint8_t fhssIdx = 0;
uint32_t nonce = 0;
uint8_t syncCounter = 0;
volatile bool txDone = false;
bool loraBusy = false;

int calMin[4] = {200,  200,  200,  200};
int calMax[4] = {3895, 3895, 3895, 3895};
int calMid[4];
float smoothed[4] = {0, 0, 0, 0};
#define SMOOTH_A 0.3f

uint16_t channels[10];

void buildUID() {
    mbedtls_md5_context ctx;
    uint8_t hash[16];
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    mbedtls_md5_update_ret(&ctx, (const uint8_t *)BINDING_PHRASE, strlen(BINDING_PHRASE));
    mbedtls_md5_finish_ret(&ctx, hash);
    mbedtls_md5_free(&ctx);
    memcpy(uid, hash, 6);
}

void buildFhssSeq() {
    uint32_t seed = ((uint32_t)uid[0] << 24) | ((uint32_t)uid[1] << 16) |
                    ((uint32_t)uid[2] << 8)  |  (uint32_t)uid[3];
    uint8_t nsPool[170];
    for (int i = 0; i < 170; i++) nsPool[i] = (i % 2 == 0) ? 0 : 2;
    for (int i = 169; i > 0; i--) {
        seed = seed * 1664525UL + 1013904223UL;
        int j = (int)(seed % (uint32_t)(i + 1));
        uint8_t tmp = nsPool[i]; nsPool[i] = nsPool[j]; nsPool[j] = tmp;
    }
    int nsIdx = 0;
    for (int i = 0; i < FHSS_SEQ_LEN; i++) {
        fhssSeq[i] = (i % 3 == 0) ? 1 : nsPool[nsIdx++];
    }
}

uint16_t crc14(const uint8_t *buf, uint8_t len) {
    uint16_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 6;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x2000) crc = (crc << 1) ^ 0x2E57;
            else              crc <<= 1;
        }
    }
    return crc & 0x3FFF;
}

void packRC(uint8_t pkt[8]) {
    uint16_t ch[4];
    for (int i = 0; i < 4; i++) {
        int mid = calMid[i];
        int raw = (int)smoothed[i];
        int v;
        if (raw <= mid) v = map(raw, calMin[i], mid, 0, 511);
        else            v = map(raw, mid, calMax[i], 511, 1023);
        ch[i] = (uint16_t)constrain(v, 0, 1023);
    }
    pkt[1] = ch[0] & 0xFF;
    pkt[2] = ((ch[0] >> 8) & 0x03) | ((ch[1] & 0x3F) << 2);
    pkt[3] = ((ch[1] >> 6) & 0x0F) | ((ch[2] & 0x0F) << 4);
    pkt[4] = ((ch[2] >> 4) & 0x3F) | ((ch[3] & 0x03) << 6);
    pkt[5] = (ch[3] >> 2) & 0xFF;
    uint8_t sw = 0;
    for (int i = 0; i < 6; i++) {
        if (channels[4 + i] >= 512) sw |= (1 << i);
    }
    pkt[6] = sw & 0x7F;
    uint16_t crc = crc14(pkt + 1, 6);
    pkt[0] = (uint8_t)(((crc >> 8) & 0x3F) << 2) | 0x00;
    pkt[7] = (uint8_t)(crc & 0xFF);
}

void packSync(uint8_t pkt[8]) {
    pkt[1] = fhssIdx;
    pkt[2] = (uint8_t)(nonce & 0xFF);
    pkt[3] = 6;
    pkt[4] = 0;
    pkt[5] = uid[4];
    pkt[6] = uid[5];
    uint16_t crc = crc14(pkt + 1, 6);
    pkt[0] = (uint8_t)(((crc >> 8) & 0x3F) << 2) | 0x02;
    pkt[7] = (uint8_t)(crc & 0xFF);
}

void IRAM_ATTR txISR() { txDone = true; }

int readAvg(int pin) {
    int s = 0;
    for (int i = 0; i < 8; i++) s += analogRead(pin);
    return s >> 3;
}

void readChannels() {
    int pins[4] = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4};
    for (int i = 0; i < 4; i++) {
        float r = (float)readAvg(pins[i]);
        if (smoothed[i] == 0.0f) smoothed[i] = r;
        smoothed[i] = SMOOTH_A * r + (1.0f - SMOOTH_A) * smoothed[i];
    }
    channels[4] = digitalRead(PIN_CH5)  ? 1023 : 0;
    channels[5] = digitalRead(PIN_CH6)  ? 1023 : 0;
    channels[6] = !digitalRead(PIN_CH7) ? 1023 : 0;
    channels[7] = !digitalRead(PIN_CH8) ? 1023 : 0;
    channels[8] = !digitalRead(PIN_CH9) ? 1023 : 0;
    channels[9] = !digitalRead(PIN_CH10)? 1023 : 0;
}

void hopFhss() {
    fhssIdx = (fhssIdx + 1) % FHSS_SEQ_LEN;
    lora.setFrequency(fhssFreqs[fhssSeq[fhssIdx]]);
}

void setup() {
    Serial.begin(115200);

    pinMode(PIN_CH1, INPUT); pinMode(PIN_CH2, INPUT);
    pinMode(PIN_CH3, INPUT); pinMode(PIN_CH4, INPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(PIN_CH5,  INPUT_PULLDOWN); pinMode(PIN_CH6,  INPUT_PULLDOWN);
    pinMode(PIN_CH7,  INPUT_PULLUP);   pinMode(PIN_CH8,  INPUT_PULLUP);
    pinMode(PIN_CH9,  INPUT_PULLUP);   pinMode(PIN_CH10, INPUT_PULLUP);

    int bootPins[4] = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4};
    for (int i = 0; i < 4; i++) calMid[i] = readAvg(bootPins[i]);

    buildUID();
    buildFhssSeq();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int state = lora.begin(fhssFreqs[fhssSeq[0]], 500.0, 7, 8, 0x12, 17, 6);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("LoRa fail: "); Serial.println(state);
        while (1) delay(1000);
    }
    lora.setCRC(false);
    lora.implicitHeader(8);
    lora.setDio0Action(txISR, RISING);
    Serial.println("TX ready");
}

void loop() {
    static uint32_t lastTx = 0;
    if (millis() - lastTx < TX_INTERVAL_MS) return;
    lastTx = millis();

    if (loraBusy) {
        if (!txDone) return;
        txDone   = false;
        loraBusy = false;
        hopFhss();
        nonce++;
    }

    readChannels();

    uint8_t pkt[8] = {};
    if (syncCounter == 0) packSync(pkt);
    else                  packRC(pkt);
    syncCounter = (syncCounter + 1) % SYNC_EVERY;

    loraBusy = true;
    txDone   = false;
    lora.startTransmit(pkt, 8);
}
