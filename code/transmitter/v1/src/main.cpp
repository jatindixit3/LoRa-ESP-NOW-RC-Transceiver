/**
 * LoRa + ESP-NOW RC Transmitter v1
 *
 * Hardware: ESP32 DevKit v1 (Wroom-32)
 * RF:       LoRa02 433MHz (SX1278)
 *
 * Channels (10 total):
 *   CH1 = GPIO34 (Aileron/Roll  - Right gimbal X)
 *   CH2 = GPIO35 (Elevator/Pitch - Right gimbal Y)
 *   CH3 = GPIO32 (Throttle      - Left gimbal Y)
 *   CH4 = GPIO33 (Rudder/Yaw    - Left gimbal X)
 *   CH5 = GPIO25 (SWA - original FS-CT6B switch)
 *   CH6 = GPIO26 (SWB - original FS-CT6B switch)
 *   CH7 = GPIO27 (AUX3 - new switch)
 *   CH8 = GPIO14 (AUX4 - new switch)
 *   CH9 = GPIO12 (AUX5 - new switch)
 *   CH10= GPIO13 (AUX6 - new switch)
 *
 * Protocol priority:
 *   1. ESP-NOW (primary, ~1-2ms latency)
 *   2. LoRa 433MHz (fallback, auto-switch after 200ms ESP-NOW silence)
 */

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <RadioLib.h>

// ─── LoRa Pin Definitions ────────────────────────────────────────────────────
#define LORA_SCK    18
#define LORA_MISO   19
#define LORA_MOSI   23
#define LORA_CS      5
#define LORA_RST     4
#define LORA_IRQ     2

// ─── Channel Input Pins ──────────────────────────────────────────────────────
// Analog (gimbal potentiometers — power pots from 3.3V for direct ADC read)
#define PIN_CH1  34   // Right gimbal X
#define PIN_CH2  35   // Right gimbal Y
#define PIN_CH3  32   // Left gimbal Y
#define PIN_CH4  33   // Left gimbal X
// Digital (switches — active HIGH, internal pull-down for SWA/SWB)
#define PIN_CH5  25   // SWA (INPUT_PULLDOWN)
#define PIN_CH6  26   // SWB (INPUT_PULLDOWN)
// Digital (new AUX — active LOW, internal pull-up)
#define PIN_CH7  27
#define PIN_CH8  14
#define PIN_CH9  12
#define PIN_CH10 13

// ─── ADC Calibration ─────────────────────────────────────────────────────────
// Adjust these min/max values after measuring your specific gimbal pot outputs
#define ADC_MIN   200    // raw ADC value at full deflection one way
#define ADC_MAX  3895    // raw ADC value at full deflection other way
#define RC_MIN  1000     // µs output at ADC_MIN
#define RC_MAX  2000     // µs output at ADC_MAX
#define RC_MID  1500

// ─── Protocol Switching ──────────────────────────────────────────────────────
#define ESPNOW_TIMEOUT_MS   200    // switch to LoRa if no ACK for this long
#define ESPNOW_RETRY_MS    2000    // while on LoRa, re-probe ESP-NOW this often

// ─── Packet Definition ───────────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint16_t channels[10];  // RC values 1000–2000 µs
    uint8_t  rssi;          // transmitter-side signal quality (0xFF = N/A)
    uint32_t seq;           // sequence number for loss counting
} RCPacket;

// ─── Globals ─────────────────────────────────────────────────────────────────
SX1278 lora = new Module(LORA_CS, LORA_IRQ, LORA_RST);

RCPacket pkt;
uint32_t seqNum = 0;

// Protocol state
enum Protocol { PROTO_ESPNOW, PROTO_LORA };
Protocol activeProto = PROTO_ESPNOW;

uint32_t lastEspNowAck  = 0;
uint32_t lastLoraProbe  = 0;

// Receiver MAC (broadcast to all peers, change to unicast for security)
uint8_t receiverMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─── Helpers ─────────────────────────────────────────────────────────────────
// Map raw ADC to 1000–2000 µs, clamped
uint16_t adcToRC(int raw) {
    int v = map(raw, ADC_MIN, ADC_MAX, RC_MIN, RC_MAX);
    return (uint16_t)constrain(v, RC_MIN, RC_MAX);
}

// Read all 10 channels
void readChannels() {
    // Analog gimbals — average 8 samples to reduce ADC noise
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

    // Original switches (active HIGH)
    pkt.channels[4] = digitalRead(PIN_CH5) ? RC_MAX : RC_MIN;
    pkt.channels[5] = digitalRead(PIN_CH6) ? RC_MAX : RC_MIN;

    // New AUX switches (active LOW with pull-up)
    pkt.channels[6] = digitalRead(PIN_CH7)  ? RC_MIN : RC_MAX;
    pkt.channels[7] = digitalRead(PIN_CH8)  ? RC_MIN : RC_MAX;
    pkt.channels[8] = digitalRead(PIN_CH9)  ? RC_MIN : RC_MAX;
    pkt.channels[9] = digitalRead(PIN_CH10) ? RC_MIN : RC_MAX;

    pkt.rssi = 0xFF;
    pkt.seq  = seqNum++;
}

// ─── ESP-NOW ─────────────────────────────────────────────────────────────────
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
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] init failed");
        return;
    }
    esp_now_register_send_cb(onEspNowSend);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiverMAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(receiverMAC)) {
        esp_now_add_peer(&peer);
    }
    lastEspNowAck = millis();  // optimistically assume in range at start
    Serial.println("[ESPNOW] initialised");
}

bool sendEspNow() {
    esp_err_t r = esp_now_send(receiverMAC, (uint8_t *)&pkt, sizeof(pkt));
    return (r == ESP_OK);
}

// ─── LoRa ────────────────────────────────────────────────────────────────────
bool loraReady = false;

void initLora() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int state = lora.begin(433.0,   // frequency MHz
                           125.0,   // bandwidth kHz
                           9,       // spreading factor (SF9 = good range/latency balance)
                           7,       // coding rate 4/7
                           0x34,    // sync word (custom, must match receiver)
                           17,      // TX power dBm
                           8);      // preamble length
    if (state == RADIOLIB_ERR_NONE) {
        loraReady = true;
        Serial.println("[LoRa] initialised");
    } else {
        Serial.printf("[LoRa] init failed: %d\n", state);
    }
}

bool sendLora() {
    if (!loraReady) return false;
    int state = lora.transmit((uint8_t *)&pkt, sizeof(pkt));
    return (state == RADIOLIB_ERR_NONE);
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("[TX] booting...");

    // Analog inputs
    pinMode(PIN_CH1, INPUT);
    pinMode(PIN_CH2, INPUT);
    pinMode(PIN_CH3, INPUT);
    pinMode(PIN_CH4, INPUT);
    analogReadResolution(12);       // 0–4095
    analogSetAttenuation(ADC_11db); // 0–3.3V range

    // Digital switch inputs
    pinMode(PIN_CH5,  INPUT_PULLDOWN);
    pinMode(PIN_CH6,  INPUT_PULLDOWN);
    pinMode(PIN_CH7,  INPUT_PULLUP);
    pinMode(PIN_CH8,  INPUT_PULLUP);
    pinMode(PIN_CH9,  INPUT_PULLUP);
    pinMode(PIN_CH10, INPUT_PULLUP);

    initEspNow();
    initLora();

    Serial.println("[TX] ready");
}

// ─── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
    static uint32_t lastTx = 0;
    const uint32_t TX_INTERVAL_MS = 14;  // ~71 Hz, matches SBUS frame rate

    if (millis() - lastTx < TX_INTERVAL_MS) return;
    lastTx = millis();

    readChannels();

    // Protocol selection
    uint32_t now = millis();
    bool espnowTimedOut = (now - lastEspNowAck) > ESPNOW_TIMEOUT_MS;

    if (activeProto == PROTO_ESPNOW) {
        if (espnowTimedOut) {
            activeProto = PROTO_LORA;
            lastLoraProbe = now;
            Serial.println("[TX] ESP-NOW timeout → switching to LoRa");
        } else {
            sendEspNow();
        }
    } else {
        // On LoRa — periodically probe ESP-NOW
        if (now - lastLoraProbe > ESPNOW_RETRY_MS) {
            lastLoraProbe = now;
            espnowSendOK = false;
            sendEspNow();
            delay(50);  // brief wait for ACK
            if (espnowSendOK) {
                activeProto = PROTO_ESPNOW;
                lastEspNowAck = millis();
                Serial.println("[TX] ESP-NOW back → switching from LoRa");
                return;
            }
        }
        sendLora();
    }
}
