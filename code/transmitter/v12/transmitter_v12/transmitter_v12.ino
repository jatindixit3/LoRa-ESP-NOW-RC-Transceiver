#include <SPI.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <esp_now.h>
#include <BleGamepad.h>
#include <WebServer.h>
#include <Preferences.h>
#include "mbedtls/md5.h"
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

#define BOOT_PIN          0
#define FHSS_NUM_CH       3
#define FHSS_SEQ_LEN      255
#define SYNC_EVERY        10
#define ESPNOW_TIMEOUT_MS 200
#define ESPNOW_RETRY_MS   2000
#define SMOOTH_A          0.3f

static const float fhssFreqs[FHSS_NUM_CH] = {433.420f, 433.920f, 434.420f};

char    bindingPhrase[64] = "my_drone_link";
int     txPower           = 20;
int     packetRateHz      = 100;
uint8_t receiverMAC[6]    = {0x58, 0xE6, 0xC5, 0x16, 0x4A, 0x58};

int   calMin[4]  = {200,  200,  200,  200};
int   calMax[4]  = {3895, 3895, 3895, 3895};
int   calMid[4];
float smoothed[4] = {0, 0, 0, 0};

SX1278   lora = new Module(LORA_CS, LORA_IRQ, LORA_RST);
uint8_t  uid[6];
uint8_t  fhssSeq[FHSS_SEQ_LEN];
uint8_t  fhssIdx  = 0;
uint32_t nonce    = 0;
uint8_t  syncCtr  = 0;
volatile bool loraTxDone = false;
bool     loraBusy  = false;
bool     loraReady = false;

bool     espnowAvailable = false;
uint32_t lastEspNowAck   = 0;
uint32_t lastLoraProbe   = 0;
bool     espnowSendOK    = false;

enum Protocol { PROTO_ESPNOW, PROTO_LORA };
Protocol activeProto = PROTO_ESPNOW;

uint16_t channels[10];
uint32_t seqNum = 0;

typedef struct __attribute__((packed)) {
    uint16_t channels[10];
    uint8_t  rssi;
    uint32_t seq;
} RCPacket;

RCPacket pkt;

BleGamepad bleGamepad("Drone TX", "ESP32", 100);
bool bleWasConnected = false;

Preferences prefs;
WebServer   server(80);

const char dashHTML[] = R"=====(
<!DOCTYPE html><html><head><title>Drone TX</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
*{box-sizing:border-box}
body{font-family:sans-serif;max-width:500px;margin:auto;padding:16px;background:#111;color:#eee}
h2{color:#0af;margin:16px 0 6px}
input,select{width:100%;padding:8px;margin:3px 0 12px;background:#222;color:#eee;border:1px solid #444;border-radius:6px;font-size:15px}
.btn{width:100%;padding:12px;background:#0af;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer;margin-top:4px}
.btn:active{opacity:.7}
.ch{display:flex;align-items:center;gap:8px;margin:3px 0}
.bar{flex:1;height:14px;background:#333;border-radius:4px;overflow:hidden}
.fill{height:100%;background:#0af;transition:width .08s}
.lbl{font-size:12px;color:#888;width:36px}
.val{font-size:12px;color:#ccc;width:42px;text-align:right}
.proto{padding:5px 10px;border-radius:4px;font-size:13px;display:inline-block;margin-bottom:6px}
.en{background:#0a3}.lo{background:#730}
</style></head><body>
<h2>Live Channels</h2>
<div id="pt"></div>
<div id="chs"></div>
<h2>Settings</h2>
<label>Binding Phrase</label><input id="bp" type="text">
<label>TX Power</label>
<select id="pwr"><option value="17">17 dBm</option><option value="20">20 dBm</option></select>
<label>Packet Rate</label>
<select id="rt"><option value="250">250 Hz</option><option value="100">100 Hz</option><option value="50">50 Hz</option><option value="25">25 Hz</option></select>
<label>Receiver MAC (e.g. 58:E6:C5:16:4A:58)</label>
<input id="mc" type="text">
<button class="btn" onclick="save()">Save &amp; Restart</button>
<script>
function load(){
  fetch('/data').then(r=>r.json()).then(d=>{
    let h='';
    for(let i=0;i<10;i++){
      let p=Math.max(0,Math.min(100,Math.round((d.ch[i]-1000)/10)));
      h+=`<div class="ch"><span class="lbl">CH${i+1}</span><div class="bar"><div class="fill" style="width:${p}%"></div></div><span class="val">${d.ch[i]}</span></div>`;
    }
    document.getElementById('chs').innerHTML=h;
    document.getElementById('bp').value=d.bp;
    document.getElementById('pwr').value=d.pwr;
    document.getElementById('rt').value=d.rate;
    document.getElementById('mc').value=d.mac;
    let pn=d.proto==='ESPNOW';
    document.getElementById('pt').innerHTML=`<span class="proto ${pn?'en':'lo'}">${pn?'ESP-NOW':'LoRa 433'}</span>`;
  });
}
function save(){
  let b={bp:document.getElementById('bp').value,
         pwr:parseInt(document.getElementById('pwr').value),
         rate:parseInt(document.getElementById('rt').value),
         mac:document.getElementById('mc').value};
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b)})
    .then(()=>alert('Saved — TX restarting'));
}
setInterval(load,100);load();
</script></body></html>
)=====";

void loadSettings() {
    prefs.begin("drontx", true);
    prefs.getString("bp", bindingPhrase, sizeof(bindingPhrase));
    if (strlen(bindingPhrase) == 0) strcpy(bindingPhrase, "my_drone_link");
    txPower      = prefs.getInt("pwr",  20);
    packetRateHz = prefs.getInt("rate", 100);
    char key[8];
    for (int i = 0; i < 6; i++) {
        snprintf(key, sizeof(key), "m%d", i);
        receiverMAC[i] = prefs.getUChar(key, receiverMAC[i]);
    }
    for (int i = 0; i < 4; i++) {
        snprintf(key, sizeof(key), "cmn%d", i);
        calMin[i] = prefs.getInt(key, calMin[i]);
        snprintf(key, sizeof(key), "cmx%d", i);
        calMax[i] = prefs.getInt(key, calMax[i]);
    }
    prefs.end();
}

void saveSettings() {
    prefs.begin("drontx", false);
    prefs.putString("bp",  bindingPhrase);
    prefs.putInt("pwr",    txPower);
    prefs.putInt("rate",   packetRateHz);
    char key[8];
    for (int i = 0; i < 6; i++) {
        snprintf(key, sizeof(key), "m%d", i);
        prefs.putUChar(key, receiverMAC[i]);
    }
    prefs.end();
}

void buildUID() {
    mbedtls_md5_context ctx;
    uint8_t hash[16];
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    mbedtls_md5_update_ret(&ctx, (const uint8_t *)bindingPhrase, strlen(bindingPhrase));
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

void packRC(uint8_t p[8]) {
    uint16_t ch[4];
    for (int i = 0; i < 4; i++) {
        ch[i] = (uint16_t)constrain((int)map((long)channels[i], 1000, 2000, 0, 1023), 0, 1023);
    }
    p[1] = ch[0] & 0xFF;
    p[2] = ((ch[0] >> 8) & 0x03) | ((ch[1] & 0x3F) << 2);
    p[3] = ((ch[1] >> 6) & 0x0F) | ((ch[2] & 0x0F) << 4);
    p[4] = ((ch[2] >> 4) & 0x3F) | ((ch[3] & 0x03) << 6);
    p[5] = (ch[3] >> 2) & 0xFF;
    uint8_t sw = 0;
    for (int i = 0; i < 6; i++) {
        if (channels[4 + i] >= 1500) sw |= (1 << i);
    }
    p[6] = sw & 0x7F;
    uint16_t crc = crc14(p + 1, 6);
    p[0] = (uint8_t)(((crc >> 8) & 0x3F) << 2) | 0x00;
    p[7] = (uint8_t)(crc & 0xFF);
}

void packSync(uint8_t p[8]) {
    p[1] = fhssIdx;
    p[2] = (uint8_t)(nonce & 0xFF);
    p[3] = 6;
    p[4] = 0;
    p[5] = uid[4];
    p[6] = uid[5];
    uint16_t crc = crc14(p + 1, 6);
    p[0] = (uint8_t)(((crc >> 8) & 0x3F) << 2) | 0x02;
    p[7] = (uint8_t)(crc & 0xFF);
}

void onEspNowSend(const uint8_t *mac, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        lastEspNowAck = millis();
        espnowSendOK  = true;
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
    lastEspNowAck    = millis();
    espnowAvailable  = true;
}

void IRAM_ATTR loraTxISR() { loraTxDone = true; }

void hopFhss() {
    fhssIdx = (fhssIdx + 1) % FHSS_SEQ_LEN;
    lora.setFrequency(fhssFreqs[fhssSeq[fhssIdx]]);
}

void initLora() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    int state = lora.begin(fhssFreqs[fhssSeq[0]], 500.0, 7, 8, 0x12, txPower, 6);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("LoRa fail: "); Serial.println(state);
        return;
    }
    lora.setCRC(false);
    lora.implicitHeader(8);
    lora.setDio0Action(loraTxISR, RISING);
    loraReady = true;
}

void initBle() {
    BleGamepadConfiguration cfg;
    cfg.setAutoReport(false);
    cfg.setControllerType(CONTROLLER_TYPE_JOYSTICK);
    cfg.setButtonCount(6);
    cfg.setHatSwitchCount(0);
    cfg.setAxesMin(-32767);
    cfg.setAxesMax(32767);
    bleGamepad.begin(&cfg);
}

void sendBle() {
    bool connected = bleGamepad.isConnected();
    if (connected != bleWasConnected) bleWasConnected = connected;
    if (!connected) return;
    bleGamepad.setX( (int16_t)map(channels[0], 1000, 2000, -32767, 32767));
    bleGamepad.setY( (int16_t)map(channels[1], 1000, 2000, -32767, 32767));
    bleGamepad.setZ( (int16_t)map(channels[2],  987, 2000, -32767, 32767));
    bleGamepad.setRZ((int16_t)map(channels[3], 1000, 2000, -32767, 32767));
    for (int i = 0; i < 6; i++) {
        if (channels[4 + i] >= 1500) bleGamepad.press(BUTTON_1 + i);
        else                         bleGamepad.release(BUTTON_1 + i);
    }
    bleGamepad.sendReport();
}

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
        int raw = (int)smoothed[i];
        int mid = calMid[i];
        int v;
        if (raw <= mid) v = map(raw, calMin[i], mid, 1000, 1500);
        else            v = map(raw, mid, calMax[i], 1500, 2000);
        channels[i] = (uint16_t)constrain(v, 1000, 2000);
    }
    channels[2]  = (uint16_t)constrain((int)channels[2], 987, 2000);
    channels[4]  = digitalRead(PIN_CH5)  ? 2000 : 1000;
    channels[5]  = digitalRead(PIN_CH6)  ? 2000 : 1000;
    channels[6]  = !digitalRead(PIN_CH7) ? 2000 : 1000;
    channels[7]  = !digitalRead(PIN_CH8) ? 2000 : 1000;
    channels[8]  = !digitalRead(PIN_CH9) ? 2000 : 1000;
    channels[9]  = !digitalRead(PIN_CH10)? 2000 : 1000;
}

bool sendEspNow() {
    for (int i = 0; i < 10; i++) pkt.channels[i] = channels[i];
    pkt.rssi = 0xFF;
    pkt.seq  = seqNum++;
    return esp_now_send(receiverMAC, (uint8_t *)&pkt, sizeof(pkt)) == ESP_OK;
}

bool sendLora() {
    if (!loraReady) return false;
    if (loraBusy) {
        if (!loraTxDone) return false;
        loraTxDone = false;
        loraBusy   = false;
        hopFhss();
        nonce++;
    }
    uint8_t p[8] = {};
    if (syncCtr == 0) packSync(p);
    else              packRC(p);
    syncCtr  = (syncCtr + 1) % SYNC_EVERY;
    loraBusy   = true;
    loraTxDone = false;
    return lora.startTransmit(p, 8) == RADIOLIB_ERR_NONE;
}

void handleRoot() {
    server.send(200, "text/html", dashHTML);
}

void handleData() {
    readChannels();
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             receiverMAC[0], receiverMAC[1], receiverMAC[2],
             receiverMAC[3], receiverMAC[4], receiverMAC[5]);
    char json[400];
    snprintf(json, sizeof(json),
        "{\"ch\":[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d],"
        "\"bp\":\"%s\",\"pwr\":%d,\"rate\":%d,"
        "\"mac\":\"%s\",\"proto\":\"%s\"}",
        channels[0], channels[1], channels[2], channels[3], channels[4],
        channels[5], channels[6], channels[7], channels[8], channels[9],
        bindingPhrase, txPower, packetRateHz, macStr,
        activeProto == PROTO_ESPNOW ? "ESPNOW" : "LORA");
    server.send(200, "application/json", json);
}

void handleSave() {
    if (!server.hasArg("plain")) { server.send(400); return; }
    String body = server.arg("plain");
    int idx;

    idx = body.indexOf("\"bp\":\"");
    if (idx >= 0) {
        int s = idx + 6, e = body.indexOf("\"", s);
        if (e > s) body.substring(s, e).toCharArray(bindingPhrase, sizeof(bindingPhrase));
    }
    idx = body.indexOf("\"pwr\":");
    if (idx >= 0) txPower = body.substring(idx + 6).toInt();
    idx = body.indexOf("\"rate\":");
    if (idx >= 0) packetRateHz = body.substring(idx + 7).toInt();
    idx = body.indexOf("\"mac\":\"");
    if (idx >= 0) {
        int s = idx + 7, e = body.indexOf("\"", s);
        if (e > s) {
            String ms = body.substring(s, e);
            sscanf(ms.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &receiverMAC[0], &receiverMAC[1], &receiverMAC[2],
                   &receiverMAC[3], &receiverMAC[4], &receiverMAC[5]);
        }
    }

    saveSettings();
    server.send(200, "text/plain", "ok");
    delay(500);
    ESP.restart();
}

void runConfigMode() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("DroneRC-Config", "12345678");
    server.on("/",     HTTP_GET,  handleRoot);
    server.on("/data", HTTP_GET,  handleData);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
    Serial.println("Config mode: connect to DroneRC-Config / 192.168.4.1");
    while (true) { server.handleClient(); delay(2); }
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
    pinMode(BOOT_PIN, INPUT_PULLUP);

    loadSettings();

    int bootPins[4] = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4};
    for (int i = 0; i < 4; i++) calMid[i] = readAvg(bootPins[i]);

    uint32_t held = 0;
    while (!digitalRead(BOOT_PIN) && held < 3000) { delay(10); held += 10; }
    if (held >= 3000) { runConfigMode(); return; }

    buildUID();
    buildFhssSeq();

    WiFi.mode(WIFI_STA);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(84);

    initEspNow();
    initLora();
    initBle();
}

void loop() {
    static uint32_t lastTx    = 0;
    static uint32_t lastPrint = 0;

    readChannels();
    sendBle();

    uint32_t now      = millis();
    uint32_t interval = (uint32_t)(1000 / packetRateHz);

    if (now - lastTx >= interval) {
        lastTx = now;

        if (activeProto == PROTO_ESPNOW) {
            if ((now - lastEspNowAck) > ESPNOW_TIMEOUT_MS) {
                activeProto   = PROTO_LORA;
                lastLoraProbe = now;
                sendLora();
            } else {
                sendEspNow();
            }
        } else {
            if (espnowAvailable && (now - lastLoraProbe) > ESPNOW_RETRY_MS) {
                lastLoraProbe = now;
                espnowSendOK  = false;
                sendEspNow();
                delay(50);
                if (espnowSendOK) {
                    activeProto   = PROTO_ESPNOW;
                    lastEspNowAck = millis();
                    return;
                }
            }
            sendLora();
        }
    }

    if (now - lastPrint > 500) {
        lastPrint = now;
        for (int i = 0; i < 10; i++) {
            Serial.print("CH"); Serial.print(i + 1);
            Serial.print(" "); Serial.println(channels[i]);
        }
        Serial.println(activeProto == PROTO_ESPNOW ? "ESPNOW" : "LORA");
        Serial.println(bleGamepad.isConnected() ? "BLE on" : "BLE adv");
        Serial.println("---");
    }
}
