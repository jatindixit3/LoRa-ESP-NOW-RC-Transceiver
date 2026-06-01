#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <RadioLib.h>

#define LORA_SCK    18
#define LORA_MISO   19
#define LORA_MOSI   23
#define LORA_CS      5
#define LORA_RST     4
#define LORA_IRQ     2

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

#define ADC_MIN   200
#define ADC_MAX  3895
#define RC_MIN  1000
#define RC_MAX  2000
#define RC_MID  1500

#define ESPNOW_TIMEOUT_MS   200
#define ESPNOW_RETRY_MS    2000

typedef struct __attribute__((packed)) {
    uint16_t channels[10];
    uint8_t  rssi;
    uint32_t seq;
} RCPacket;

SX1278 lora = new Module(LORA_CS, LORA_IRQ, LORA_RST);

RCPacket pkt;
uint32_t seqNum = 0;

enum Protocol { PROTO_ESPNOW, PROTO_LORA };
Protocol activeProto = PROTO_ESPNOW;

uint32_t lastEspNowAck = 0;
uint32_t lastLoraProbe = 0;

uint8_t receiverMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint16_t adcToRC(int raw) {
    int v = map(raw, ADC_MIN, ADC_MAX, RC_MIN, RC_MAX);
    return (uint16_t)constrain(v, RC_MIN, RC_MAX);
}

void readChannels() {
    int s1=0, s2=0, s3=0, s4=0;
    for (int i = 0; i < 8; i++) {
        s1 += analogRead(PIN_CH1);
        s2 += analogRead(PIN_CH2);
        s3 += analogRead(PIN_CH3);
        s4 += analogRead(PIN_CH4);
    }
    pkt.channels[0] = adcToRC(s1 / 8);
    pkt.channels[1] = adcToRC(s2 / 8);
    pkt.channels[2] = adcToRC(s3 / 8);
    pkt.channels[3] = adcToRC(s4 / 8);

    pkt.channels[4] = digitalRead(PIN_CH5) ? RC_MAX : RC_MIN;
    pkt.channels[5] = digitalRead(PIN_CH6) ? RC_MAX : RC_MIN;
    pkt.channels[6] = digitalRead(PIN_CH7)  ? RC_MIN : RC_MAX;
    pkt.channels[7] = digitalRead(PIN_CH8)  ? RC_MIN : RC_MAX;
    pkt.channels[8] = digitalRead(PIN_CH9)  ? RC_MIN : RC_MAX;
    pkt.channels[9] = digitalRead(PIN_CH10) ? RC_MIN : RC_MAX;

    pkt.rssi = 0xFF;
    pkt.seq  = seqNum++;
}

bool espnowSendOK = false;

void onEspNowSend(const uint8_t *mac, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        lastEspNowAck = millis();
        espnowSendOK = true;
    }
}

void initEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_now_init();
    esp_now_register_send_cb(onEspNowSend);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiverMAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(receiverMAC)) {
        esp_now_add_peer(&peer);
    }
    lastEspNowAck = millis();
}

bool sendEspNow() {
    return esp_now_send(receiverMAC, (uint8_t *)&pkt, sizeof(pkt)) == ESP_OK;
}

bool loraReady = false;

void initLora() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int state = lora.begin(433.0, 125.0, 9, 7, 0x34, 17, 8);
    if (state == RADIOLIB_ERR_NONE) loraReady = true;
}

bool sendLora() {
    if (!loraReady) return false;
    return lora.transmit((uint8_t *)&pkt, sizeof(pkt)) == RADIOLIB_ERR_NONE;
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_CH1, INPUT);
    pinMode(PIN_CH2, INPUT);
    pinMode(PIN_CH3, INPUT);
    pinMode(PIN_CH4, INPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(PIN_CH5,  INPUT_PULLDOWN);
    pinMode(PIN_CH6,  INPUT_PULLDOWN);
    pinMode(PIN_CH7,  INPUT_PULLUP);
    pinMode(PIN_CH8,  INPUT_PULLUP);
    pinMode(PIN_CH9,  INPUT_PULLUP);
    pinMode(PIN_CH10, INPUT_PULLUP);
    initEspNow();
    initLora();
}

void loop() {
    static uint32_t lastTx = 0;
    if (millis() - lastTx < 14) return;
    lastTx = millis();

    readChannels();

    uint32_t now = millis();
    bool espnowTimedOut = (now - lastEspNowAck) > ESPNOW_TIMEOUT_MS;

    if (activeProto == PROTO_ESPNOW) {
        if (espnowTimedOut) {
            activeProto = PROTO_LORA;
            lastLoraProbe = now;
        } else {
            sendEspNow();
        }
    } else {
        if (now - lastLoraProbe > ESPNOW_RETRY_MS) {
            lastLoraProbe = now;
            espnowSendOK = false;
            sendEspNow();
            delay(50);
            if (espnowSendOK) {
                activeProto = PROTO_ESPNOW;
                lastEspNowAck = millis();
                return;
            }
        }
        sendLora();
    }
}
