#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <RadioLib.h>

#define LORA_SCK   19
#define LORA_MISO  20
#define LORA_MOSI  21
#define LORA_CS     3
#define LORA_RST    0
#define LORA_IRQ    1

#define SBUS_TX_PIN    2
#define SBUS_BAUD      100000
#define SBUS_NUM_CH    16
#define SBUS_FRAME_LEN 25
#define SBUS_CH_MID    992
#define SBUS_CH_MIN    172
#define SBUS_CH_MAX    1811

#define FAILSAFE_TIMEOUT_MS 500

typedef struct __attribute__((packed)) {
    uint16_t channels[10];
    uint8_t  rssi;
    uint32_t seq;
} RCPacket;

SX1278 lora = new Module(LORA_CS, LORA_IRQ, LORA_RST);

uint8_t transmitterMAC[] = {0x10, 0x06, 0x1C, 0xF6, 0x7B, 0x10};

RCPacket lastPkt;
uint32_t lastPacketTime = 0;

uint16_t rcToSbus(uint16_t rc_us) {
    int v = map((int)rc_us, 1000, 2000, SBUS_CH_MIN, SBUS_CH_MAX);
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
        d[byteIdx]   |= (val << bitIdx) & 0xFF;
        d[byteIdx+1] |= (val >> (8 - bitIdx)) & 0xFF;
        if (bitIdx > 5)
            d[byteIdx+2] |= (val >> (16 - bitIdx)) & 0xFF;
    }
    frame[23] = (frameLost ? 0x20 : 0x00) | (failsafe ? 0x10 : 0x00);
    frame[24] = 0x00;
}

void sendSbus(bool frameLost, bool failsafe) {
    uint16_t sbCh[SBUS_NUM_CH];
    for (int i = 0; i < 10; i++) sbCh[i] = rcToSbus(lastPkt.channels[i]);
    for (int i = 10; i < SBUS_NUM_CH; i++) sbCh[i] = SBUS_CH_MID;
    uint8_t frame[SBUS_FRAME_LEN];
    buildSbusFrame(frame, sbCh, frameLost, failsafe);
    Serial1.write(frame, SBUS_FRAME_LEN);
}

void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(RCPacket)) return;
    memcpy(&lastPkt, data, sizeof(RCPacket));
    lastPacketTime = millis();
}

void initEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, transmitterMAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    esp_now_register_recv_cb(onEspNowRecv);
    Serial.println("ESP-NOW ready");
}

bool loraReady = false;
volatile bool loraRxDone = false;

void IRAM_ATTR loraISR() {
    loraRxDone = true;
}

void initLora() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int state = lora.begin(433.0, 125.0, 7, 7, 0x34, 17, 8);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("LoRa init failed: ");
        Serial.println(state);
        return;
    }
    lora.setDio0Action(loraISR, RISING);
    state = lora.startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        loraReady = true;
        Serial.println("LoRa ready");
    }
}

void checkLora() {
    if (!loraReady || !loraRxDone) return;
    loraRxDone = false;
    RCPacket p;
    int state = lora.readData((uint8_t *)&p, sizeof(p));
    if (state == RADIOLIB_ERR_NONE && sizeof(p) == sizeof(RCPacket)) {
        memcpy(&lastPkt, &p, sizeof(RCPacket));
        lastPacketTime = millis();
    }
    lora.startReceive();
}

void setup() {
    delay(2000);
    Serial.begin(115200);
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
    Serial1.begin(SBUS_BAUD, SERIAL_8E2, -1, SBUS_TX_PIN, true);
    for (int i = 0; i < 10; i++) lastPkt.channels[i] = 1500;
    lastPkt.channels[2] = 987;
    lastPacketTime = millis();
    initEspNow();
    initLora();
}

void loop() {
    static uint32_t lastSbusTx = 0;
    checkLora();
    uint32_t now = millis();
    bool timedOut  = (now - lastPacketTime) > FAILSAFE_TIMEOUT_MS;
    bool frameLost = (now - lastPacketTime) > 28;
    if (now - lastSbusTx >= 14) {
        lastSbusTx = now;
        sendSbus(frameLost, timedOut);
    }
}
