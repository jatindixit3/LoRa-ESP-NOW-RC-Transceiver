#include <SPI.h>
#include <RadioLib.h>
#include <WiFi.h>
#include "mbedtls/md5.h"

#define LORA_SCK    4
#define LORA_MISO   5
#define LORA_MOSI   6
#define LORA_CS     7
#define LORA_RST    8
#define LORA_IRQ    9

#define SBUS_TX_PIN     10
#define SBUS_BAUD       100000
#define SBUS_NUM_CH     16
#define SBUS_FRAME_LEN  25
#define SBUS_CH_MID     992
#define SBUS_CH_MIN     172
#define SBUS_CH_MAX     1811

#define BINDING_PHRASE  "my_drone_link"
#define FHSS_NUM_CH     3
#define FHSS_SEQ_LEN    255
#define HOP_TIMEOUT_MS  15
#define FAILSAFE_MS     500
#define FRAMELOST_MS    28

static const float fhssFreqs[FHSS_NUM_CH] = {433.420f, 433.920f, 434.420f};

SX1278 lora = new Module(LORA_CS, LORA_IRQ, LORA_RST);

uint8_t uid[6];
uint8_t fhssSeq[FHSS_SEQ_LEN];
uint8_t fhssIdx = 0;
uint32_t lastPacketTime = 0;
uint32_t lastHopTime = 0;

uint16_t rcChannels[10];
bool loraReady = false;
volatile bool loraRxDone = false;

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

bool verifyCRC(const uint8_t pkt[8]) {
    uint16_t computed = crc14(pkt + 1, 6);
    uint8_t hiReceived = (pkt[0] >> 2) & 0x3F;
    uint8_t loReceived = pkt[7];
    return (hiReceived == (uint8_t)((computed >> 8) & 0x3F)) &&
           (loReceived == (uint8_t)(computed & 0xFF));
}

uint8_t packetType(const uint8_t pkt[8]) {
    return pkt[0] & 0x03;
}

void decodeRC(const uint8_t pkt[8]) {
    uint16_t ch[4];
    ch[0] = ((uint16_t)pkt[1]) | (((uint16_t)pkt[2] & 0x03) << 8);
    ch[1] = ((uint16_t)pkt[2] >> 2) | (((uint16_t)pkt[3] & 0x0F) << 6);
    ch[2] = ((uint16_t)pkt[3] >> 4) | (((uint16_t)pkt[4] & 0x3F) << 4);
    ch[3] = ((uint16_t)pkt[4] >> 6) | ((uint16_t)pkt[5] << 2);
    for (int i = 0; i < 4; i++) {
        ch[i] &= 0x3FF;
        rcChannels[i] = (uint16_t)map((long)ch[i], 0, 1023, 1000, 2000);
    }
    uint8_t sw = pkt[6] & 0x7F;
    for (int i = 0; i < 6; i++) {
        rcChannels[4 + i] = (sw & (1 << i)) ? 2000 : 1000;
    }
}

void decodeSync(const uint8_t pkt[8]) {
    uint8_t newIdx = pkt[1];
    if (newIdx < FHSS_SEQ_LEN) fhssIdx = newIdx;
}

void hopFhss() {
    fhssIdx = (fhssIdx + 1) % FHSS_SEQ_LEN;
    lora.setFrequency(fhssFreqs[fhssSeq[fhssIdx]]);
}

void IRAM_ATTR loraISR() { loraRxDone = true; }

uint16_t rcToSbus(uint16_t rc) {
    int v = map((int)rc, 1000, 2000, SBUS_CH_MIN, SBUS_CH_MAX);
    return (uint16_t)constrain(v, SBUS_CH_MIN, SBUS_CH_MAX);
}

void buildSbusFrame(uint8_t frame[SBUS_FRAME_LEN], uint16_t ch[SBUS_NUM_CH], bool frameLost, bool failsafe) {
    frame[0] = 0x0F;
    uint8_t *d = &frame[1];
    memset(d, 0, 22);
    for (int i = 0; i < SBUS_NUM_CH; i++) {
        uint16_t val = ch[i];
        int bitpos = i * 11;
        int byteIdx = bitpos / 8;
        int bitIdx  = bitpos % 8;
        d[byteIdx]   |= (uint8_t)((val << bitIdx) & 0xFF);
        d[byteIdx+1] |= (uint8_t)((val >> (8 - bitIdx)) & 0xFF);
        if (bitIdx > 5) d[byteIdx+2] |= (uint8_t)((val >> (16 - bitIdx)) & 0xFF);
    }
    frame[23] = (frameLost ? 0x20 : 0x00) | (failsafe ? 0x10 : 0x00);
    frame[24] = 0x00;
}

void sendSbus(bool frameLost, bool failsafe) {
    uint16_t sbCh[SBUS_NUM_CH];
    for (int i = 0; i < 10; i++) sbCh[i] = rcToSbus(rcChannels[i]);
    for (int i = 10; i < SBUS_NUM_CH; i++) sbCh[i] = SBUS_CH_MID;
    uint8_t frame[SBUS_FRAME_LEN];
    buildSbusFrame(frame, sbCh, frameLost, failsafe);
    Serial1.write(frame, SBUS_FRAME_LEN);
}

void setup() {
    delay(2000);
    Serial.begin(115200);
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());

    Serial1.begin(SBUS_BAUD, SERIAL_8E2, -1, SBUS_TX_PIN, true);

    for (int i = 0; i < 10; i++) rcChannels[i] = 1500;
    rcChannels[2] = 987;
    lastPacketTime = millis();

    buildUID();
    buildFhssSeq();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int state = lora.begin(fhssFreqs[fhssSeq[0]], 500.0, 7, 8, 0x12, 17, 6);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("LoRa fail: "); Serial.println(state);
        return;
    }
    lora.setCRC(false);
    lora.implicitHeader(8);
    lora.setDio0Action(loraISR, RISING);
    state = lora.startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        loraReady = true;
        Serial.println("RX ready");
    }
    lastHopTime = millis();
}

void loop() {
    static uint32_t lastSbus = 0;

    if (loraReady && loraRxDone) {
        loraRxDone = false;
        uint8_t pkt[8];
        int state = lora.readData(pkt, 8);
        if (state == RADIOLIB_ERR_NONE && verifyCRC(pkt)) {
            uint8_t pt = packetType(pkt);
            if (pt == 0x00) {
                decodeRC(pkt);
                lastPacketTime = millis();
            } else if (pt == 0x02) {
                decodeSync(pkt);
                lastPacketTime = millis();
            }
            hopFhss();
            lastHopTime = millis();
        }
        lora.startReceive();
    }

    if (loraReady && (millis() - lastHopTime > HOP_TIMEOUT_MS)) {
        hopFhss();
        lastHopTime = millis();
        lora.startReceive();
    }

    uint32_t now = millis();
    bool timedOut  = (now - lastPacketTime) > FAILSAFE_MS;
    bool frameLost = (now - lastPacketTime) > FRAMELOST_MS;

    if (now - lastSbus >= 14) {
        lastSbus = now;
        sendSbus(frameLost, timedOut);
    }
}
