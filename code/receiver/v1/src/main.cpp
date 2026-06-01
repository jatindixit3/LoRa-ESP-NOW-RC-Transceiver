/**
 * LoRa + ESP-NOW RC Receiver v1
 *
 * Hardware: ESP32-C3 Super Mini
 * RF:       LoRa02 433MHz (SX1278)
 * Output:   SBUS (100000 baud, 8E2, inverted) on GPIO10
 *
 * LoRa Wiring:
 *   VCC  → 3.3V
 *   GND  → GND
 *   SCK  → GPIO4
 *   MISO → GPIO5
 *   MOSI → GPIO6
 *   NSS  → GPIO7
 *   RST  → GPIO8
 *   DIO0 → GPIO9
 *
 * SBUS output: GPIO10 (UART1 TX, hardware inverted)
 *
 * Protocol priority:
 *   1. ESP-NOW (primary, ~1-2ms latency)
 *   2. LoRa 433MHz (fallback, auto-detected by packet absence)
 */

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <SPI.h>
#include <RadioLib.h>

// ─── LoRa Pin Definitions ────────────────────────────────────────────────────
#define LORA_SCK   4
#define LORA_MISO  5
#define LORA_MOSI  6
#define LORA_CS    7
#define LORA_RST   8
#define LORA_IRQ   9

// ─── SBUS Output ─────────────────────────────────────────────────────────────
#define SBUS_TX_PIN  10

// ─── SBUS Protocol ───────────────────────────────────────────────────────────
#define SBUS_BAUD       100000
#define SBUS_NUM_CH     16
#define SBUS_FRAME_LEN  25
#define SBUS_START_BYTE 0x0F
#define SBUS_END_BYTE   0x00
#define SBUS_CH_MID     992
#define SBUS_CH_MIN     172
#define SBUS_CH_MAX    1811

// ─── Failsafe ────────────────────────────────────────────────────────────────
#define FAILSAFE_TIMEOUT_MS  500   // assert SBUS failsafe if no packet for this long

// ─── Packet Definition (must match transmitter) ──────────────────────────────
typedef struct __attribute__((packed)) {
    uint16_t channels[10];
    uint8_t  rssi;
    uint32_t seq;
} RCPacket;

// ─── Globals ─────────────────────────────────────────────────────────────────
SPIClass spi1(FSPI);
SX1278 lora = new Module(LORA_CS, LORA_IRQ, LORA_RST, RADIOLIB_NC, spi1);

RCPacket lastPkt;
uint32_t lastPacketTime = 0;
bool     newPacketReady = false;

// ─── SBUS Encoding ───────────────────────────────────────────────────────────
// Map RC microseconds (1000–2000) to SBUS range (172–1811)
uint16_t rcToSbus(uint16_t rc_us) {
    int v = map((int)rc_us, 1000, 2000, SBUS_CH_MIN, SBUS_CH_MAX);
    return (uint16_t)constrain(v, SBUS_CH_MIN, SBUS_CH_MAX);
}

void buildSbusFrame(uint8_t frame[SBUS_FRAME_LEN], uint16_t ch[SBUS_NUM_CH],
                    bool frameLost, bool failsafe) {
    frame[0] = SBUS_START_BYTE;

    // Pack 16 × 11-bit channels into 22 bytes
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
    frame[24] = SBUS_END_BYTE;
}

void sendSbus(bool frameLost, bool failsafe) {
    uint16_t sbCh[SBUS_NUM_CH];

    // Fill received channels
    for (int i = 0; i < 10; i++)
        sbCh[i] = rcToSbus(lastPkt.channels[i]);

    // Unused channels set to mid
    for (int i = 10; i < SBUS_NUM_CH; i++)
        sbCh[i] = SBUS_CH_MID;

    uint8_t frame[SBUS_FRAME_LEN];
    buildSbusFrame(frame, sbCh, frameLost, failsafe);
    Serial1.write(frame, SBUS_FRAME_LEN);
}

// ─── ESP-NOW ─────────────────────────────────────────────────────────────────
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != sizeof(RCPacket)) return;
    memcpy(&lastPkt, data, sizeof(RCPacket));
    lastPacketTime = millis();
    newPacketReady = true;
}

void initEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] init failed");
        return;
    }
    esp_now_register_recv_cb(onEspNowRecv);
    Serial.println("[ESPNOW] initialised (listening)");
}

// ─── LoRa ────────────────────────────────────────────────────────────────────
bool loraReady = false;
volatile bool loraRxDone = false;

void loraISR() {
    loraRxDone = true;
}

void initLora() {
    spi1.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int state = lora.begin(433.0,   // frequency MHz — must match TX
                           125.0,   // bandwidth kHz
                           9,       // spreading factor SF9
                           7,       // coding rate 4/7
                           0x34,    // sync word — must match TX
                           17,      // not used in RX, but set for symmetry
                           8);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LoRa] init failed: %d\n", state);
        return;
    }
    lora.setDio0Action(loraISR, RISING);
    state = lora.startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        loraReady = true;
        Serial.println("[LoRa] initialised, listening");
    } else {
        Serial.printf("[LoRa] startReceive failed: %d\n", state);
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
        newPacketReady = true;
    }
    // Re-arm continuous receive
    lora.startReceive();
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("[RX] booting...");

    // SBUS UART — inverted TX on GPIO10
    Serial1.begin(SBUS_BAUD, SERIAL_8E2, -1, SBUS_TX_PIN, true /* invert */);

    // Pre-fill packet with safe mid values
    for (int i = 0; i < 10; i++) lastPkt.channels[i] = 1500;
    lastPkt.channels[2] = 1000;  // Throttle to minimum at boot
    lastPacketTime = millis();

    initEspNow();
    initLora();

    Serial.println("[RX] ready");
}

// ─── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
    static uint32_t lastSbusTx = 0;
    const uint32_t SBUS_INTERVAL_MS = 14;  // 71 Hz

    checkLora();

    uint32_t now = millis();
    bool timedOut  = (now - lastPacketTime) > FAILSAFE_TIMEOUT_MS;
    bool frameLost = (now - lastPacketTime) > (SBUS_INTERVAL_MS * 2);

    if (now - lastSbusTx >= SBUS_INTERVAL_MS) {
        lastSbusTx = now;
        sendSbus(frameLost, timedOut);

        if (timedOut) {
            Serial.println("[RX] FAILSAFE — no packet received");
        }
    }
}
