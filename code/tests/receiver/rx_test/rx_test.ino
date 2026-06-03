#include <WiFi.h>
#include <esp_now.h>

typedef struct __attribute__((packed)) {
    uint16_t channels[10];
    uint8_t  rssi;
    uint32_t seq;
} RCPacket;

void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(RCPacket)) return;
    RCPacket pkt;
    memcpy(&pkt, data, sizeof(RCPacket));
    Serial.print("PKT seq="); Serial.print(pkt.seq);
    Serial.print(" CH1="); Serial.print(pkt.channels[0]);
    Serial.print(" CH2="); Serial.print(pkt.channels[1]);
    Serial.print(" CH3="); Serial.print(pkt.channels[2]);
    Serial.print(" CH4="); Serial.println(pkt.channels[3]);
}

void setup() {
    delay(2000);
    Serial.begin(115200);
    delay(500);
    Serial.println("RX TEST BOOT");
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init FAILED");
        return;
    }
    esp_now_register_recv_cb(onEspNowRecv);
    Serial.println("ESP-NOW ready — waiting for packets");
}

void loop() {
}
