#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <RadioLib.h>
#include <BleGamepad.h>
#include "esp_wifi.h"

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

#define RC_MIN 1000
#define RC_MAX 2000

#define ESPNOW_TIMEOUT_MS 200
#define ESPNOW_RETRY_MS  2000

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

bool espnowAvailable = false;
uint32_t lastEspNowAck = 0;
uint32_t lastLoraProbe = 0;
bool loraReady = false;
volatile bool loraTxDone = false;
bool loraBusy = false;

uint8_t receiverMAC[] = {0x98, 0x3D, 0xAE, 0x60, 0x15, 0x88};

const int rcMidDefault[4] = {1500, 1500, 987, 1500};
const int rcMinDefault[4] = {1000, 1000, 987, 1000};
int calMin[4] = {200, 200, 200, 200};
int calMax[4] = {3895, 3895, 3895, 3895};
int calMid[4];
float smoothedADC[4] = {0, 0, 0, 0};

#define SMOOTH_ALPHA 0.3f

BleGamepad bleGamepad("Drone TX", "ESP32", 100);
bool bleWasConnected = false;

int readAvg(int pin) {
    int s = 0;
    for (int i = 0; i < 8; i++) s += analogRead(pin);
    return s / 8;
}

uint16_t adcToRC(int raw, int ch) {
    int mn  = calMin[ch];
    int mx  = calMax[ch];
    int mid = calMid[ch];
    int rcMid = rcMidDefault[ch];
    int rcMin = rcMinDefault[ch];
    int v = (raw <= mid)
        ? map(raw, mn, mid, rcMin, rcMid)
        : map(raw, mid, mx, rcMid, RC_MAX);
    v = constrain(v, rcMin, RC_MAX);
    if (ch == 0) v = rcMin + RC_MAX - v;
    return (uint16_t)v;
}

void readChannels() {
    int pins[4] = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4};
    for (int i = 0; i < 4; i++) {
        float raw = (float)readAvg(pins[i]);
        if (smoothedADC[i] == 0.0f) smoothedADC[i] = raw;
        smoothedADC[i] = SMOOTH_ALPHA * raw + (1.0f - SMOOTH_ALPHA) * smoothedADC[i];
        pkt.channels[i] = adcToRC((int)smoothedADC[i], i);
    }
    pkt.channels[4] = digitalRead(PIN_CH5)  ? RC_MAX : RC_MIN;
    pkt.channels[5] = digitalRead(PIN_CH6)  ? RC_MAX : RC_MIN;
    pkt.channels[6] = digitalRead(PIN_CH7)  ? RC_MAX : RC_MIN;
    pkt.channels[7] = digitalRead(PIN_CH8)  ? RC_MAX : RC_MIN;
    pkt.channels[8] = digitalRead(PIN_CH9)  ? RC_MAX : RC_MIN;
    pkt.channels[9] = digitalRead(PIN_CH10) ? RC_MAX : RC_MIN;
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
    if (esp_now_init() != ESP_OK) {
        activeProto = PROTO_LORA;
        return;
    }
    esp_now_register_send_cb(onEspNowSend);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiverMAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(receiverMAC)) esp_now_add_peer(&peer);
    lastEspNowAck = millis();
    espnowAvailable = true;
    Serial.println("ESP-NOW ready");
}

void IRAM_ATTR loraTxISR() { loraTxDone = true; }

void initLora() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int state = lora.begin(433.0, 125.0, 7, 7, 0x34, 17, 8);
    if (state == RADIOLIB_ERR_NONE) {
        lora.setDio0Action(loraTxISR, RISING);
        loraReady = true;
        Serial.println("LoRa ready");
    } else {
        Serial.print("LoRa init failed: ");
        Serial.println(state);
    }
}

void initBle() {
    BleGamepadConfiguration bgCfg;
    bgCfg.setAutoReport(false);
    bgCfg.setControllerType(CONTROLLER_TYPE_JOYSTICK);
    bgCfg.setButtonCount(6);
    bgCfg.setHatSwitchCount(0);
    bgCfg.setAxesMin(-32767);
    bgCfg.setAxesMax(32767);
    bleGamepad.begin(&bgCfg);
    Serial.println("BLE started");
}

void sendBle() {
    bool connected = bleGamepad.isConnected();
    if (connected != bleWasConnected) {
        bleWasConnected = connected;
        Serial.println(connected ? "BLE connected" : "BLE disconnected");
    }
    if (!connected) return;
    bleGamepad.setX( (int16_t)map(pkt.channels[0], 1000, 2000, -32767, 32767));
    bleGamepad.setY( (int16_t)map(pkt.channels[1], 1000, 2000, -32767, 32767));
    bleGamepad.setZ( (int16_t)map(pkt.channels[2],  987, 2000, -32767, 32767));
    bleGamepad.setRZ((int16_t)map(pkt.channels[3], 1000, 2000, -32767, 32767));
    for (int i = 0; i < 6; i++) {
        if (pkt.channels[4 + i] >= 1500)
            bleGamepad.press(BUTTON_1 + i);
        else
            bleGamepad.release(BUTTON_1 + i);
    }
    bleGamepad.sendReport();
}

bool sendEspNow() {
    return esp_now_send(receiverMAC, (uint8_t *)&pkt, sizeof(pkt)) == ESP_OK;
}

bool sendLora() {
    if (!loraReady) return false;
    if (loraBusy) {
        if (!loraTxDone) return false;
        loraTxDone = false;
        loraBusy = false;
    }
    loraBusy = true;
    loraTxDone = false;
    return lora.startTransmit((uint8_t *)&pkt, sizeof(pkt)) == RADIOLIB_ERR_NONE;
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

    int bootPins[4] = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4};
    for (int i = 0; i < 4; i++) calMid[i] = readAvg(bootPins[i]);

    pkt.channels[0] = 1500;
    pkt.channels[1] = 1500;
    pkt.channels[2] = 987;
    pkt.channels[3] = 1500;
    pkt.channels[4] = digitalRead(PIN_CH5)  ? RC_MAX : RC_MIN;
    pkt.channels[5] = digitalRead(PIN_CH6)  ? RC_MAX : RC_MIN;
    pkt.channels[6] = digitalRead(PIN_CH7)  ? RC_MAX : RC_MIN;
    pkt.channels[7] = digitalRead(PIN_CH8)  ? RC_MAX : RC_MIN;
    pkt.channels[8] = digitalRead(PIN_CH9)  ? RC_MAX : RC_MIN;
    pkt.channels[9] = digitalRead(PIN_CH10) ? RC_MAX : RC_MIN;
    pkt.rssi = 0xFF;
    pkt.seq  = 0;

    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(84);

    initEspNow();
    initLora();
    initBle();
}

void loop() {
    static uint32_t lastPrint = 0;

    readChannels();
    sendBle();

    if (millis() - lastPrint > 500) {
        lastPrint = millis();
        for (int i = 0; i < 10; i++) {
            Serial.print("CH"); Serial.print(i + 1);
            Serial.print(" "); Serial.println(pkt.channels[i]);
        }
        Serial.print("PROTO "); Serial.println(activeProto == PROTO_ESPNOW ? "ESP-NOW" : "LoRa");
        Serial.print("BLE   "); Serial.println(bleWasConnected ? "connected" : "advertising");
        Serial.println("---");
    }

    uint32_t now = millis();

    if (activeProto == PROTO_ESPNOW) {
        if ((now - lastEspNowAck) > ESPNOW_TIMEOUT_MS) {
            activeProto = PROTO_LORA;
            lastLoraProbe = now;
            Serial.println("ESP-NOW timeout — switching to LoRa");
        } else {
            sendEspNow();
        }
    } else {
        if (espnowAvailable && (now - lastLoraProbe) > ESPNOW_RETRY_MS) {
            lastLoraProbe = now;
            espnowSendOK = false;
            sendEspNow();
            delay(50);
            if (espnowSendOK) {
                activeProto = PROTO_ESPNOW;
                lastEspNowAck = millis();
                Serial.println("ESP-NOW back — switching from LoRa");
                return;
            }
        }
        sendLora();
    }
}
