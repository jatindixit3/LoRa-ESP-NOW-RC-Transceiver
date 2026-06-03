#include <esp_now.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <RadioLib.h>
#include <EEPROM.h>

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

#define RC_MIN  1000
#define RC_MAX  2000
#define RC_MID  1500

#define ESPNOW_TIMEOUT_MS  200
#define ESPNOW_RETRY_MS   2000

#define EEPROM_SIZE     256
#define EEPROM_MAGIC    0xAB
#define EEPROM_ADDR_MAGIC 0

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

int calMin[4];
int calMax[4];
int calMid[4];
bool reversed[10];
int trim[10];
float expo[4];

WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

int rawADC[4];

int readAvg(int pin) {
    int s = 0;
    for (int i = 0; i < 8; i++) s += analogRead(pin);
    return s / 8;
}

float applyExpo(float normalised, float expoVal) {
    return normalised * (1.0f - expoVal) + (normalised * normalised * normalised) * expoVal;
}

uint16_t adcToRC(int raw, int ch) {
    int mn = calMin[ch];
    int mx = calMax[ch];
    int mid = calMid[ch];
    int v;
    if (raw <= mid) {
        v = map(raw, mn, mid, RC_MIN, RC_MID);
    } else {
        v = map(raw, mid, mx, RC_MID, RC_MAX);
    }
    v = constrain(v, RC_MIN, RC_MAX);
    float norm = ((float)v - RC_MID) / (float)(RC_MID - RC_MIN);
    norm = constrain(norm, -1.0f, 1.0f);
    norm = applyExpo(norm, expo[ch]);
    int mapped = (int)(RC_MID + norm * (float)(RC_MID - RC_MIN));
    mapped = constrain(mapped, RC_MIN, RC_MAX);
    if (reversed[ch]) mapped = RC_MIN + RC_MAX - mapped;
    mapped += trim[ch];
    mapped = constrain(mapped, RC_MIN, RC_MAX);
    return (uint16_t)mapped;
}

uint16_t digitalToRC(int pin, bool rev, int trimVal) {
    int v = digitalRead(pin) ? RC_MAX : RC_MIN;
    if (rev) v = RC_MIN + RC_MAX - v;
    v += trimVal;
    v = constrain(v, RC_MIN, RC_MAX);
    return (uint16_t)v;
}

void readChannels() {
    int analogPins[4] = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4};
    for (int i = 0; i < 4; i++) {
        rawADC[i] = readAvg(analogPins[i]);
        pkt.channels[i] = adcToRC(rawADC[i], i);
    }
    pkt.channels[4] = digitalToRC(PIN_CH5,  reversed[4], trim[4]);
    pkt.channels[5] = digitalToRC(PIN_CH6,  reversed[5], trim[5]);
    pkt.channels[6] = digitalToRC(PIN_CH7,  reversed[6], trim[6]);
    pkt.channels[7] = digitalToRC(PIN_CH8,  reversed[7], trim[7]);
    pkt.channels[8] = digitalToRC(PIN_CH9,  reversed[8], trim[8]);
    pkt.channels[9] = digitalToRC(PIN_CH10, reversed[9], trim[9]);
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
        Serial.println("ESP-NOW init failed — using LoRa only");
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

void IRAM_ATTR loraTxISR() {
    loraTxDone = true;
}

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

void printChannels() {
    Serial.println("CH    VALUE  RAW");
    Serial.println("----------------");
    int pins[4] = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4};
    for (int i = 0; i < 4; i++) {
        Serial.print("CH"); Serial.print(i+1); Serial.print("   ");
        Serial.print(pkt.channels[i]); Serial.print("   ");
        Serial.println(rawADC[i]);
    }
    Serial.print("CH5   "); Serial.println(pkt.channels[4]);
    Serial.print("CH6   "); Serial.println(pkt.channels[5]);
    Serial.print("CH7   "); Serial.println(pkt.channels[6]);
    Serial.print("CH8   "); Serial.println(pkt.channels[7]);
    Serial.print("CH9   "); Serial.println(pkt.channels[8]);
    Serial.print("CH10  "); Serial.println(pkt.channels[9]);
    Serial.print("PROTO "); Serial.println(activeProto == PROTO_ESPNOW ? "ESP-NOW" : "LoRa");
    Serial.println("================");
}

void eepromWriteDefaults() {
    EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
    int addr = 1;
    for (int i = 0; i < 4; i++) {
        EEPROM.put(addr, (int)200);  addr += sizeof(int);
        EEPROM.put(addr, (int)3895); addr += sizeof(int);
        EEPROM.put(addr, (int)2047); addr += sizeof(int);
    }
    for (int i = 0; i < 10; i++) {
        EEPROM.put(addr, (bool)false); addr += sizeof(bool);
    }
    for (int i = 0; i < 10; i++) {
        EEPROM.put(addr, (int)0); addr += sizeof(int);
    }
    for (int i = 0; i < 4; i++) {
        EEPROM.put(addr, (float)0.0f); addr += sizeof(float);
    }
    EEPROM.commit();
}

void eepromLoad() {
    int addr = 1;
    for (int i = 0; i < 4; i++) {
        EEPROM.get(addr, calMin[i]);  addr += sizeof(int);
        EEPROM.get(addr, calMax[i]);  addr += sizeof(int);
        EEPROM.get(addr, calMid[i]);  addr += sizeof(int);
    }
    for (int i = 0; i < 10; i++) {
        EEPROM.get(addr, reversed[i]); addr += sizeof(bool);
    }
    for (int i = 0; i < 10; i++) {
        EEPROM.get(addr, trim[i]); addr += sizeof(int);
    }
    for (int i = 0; i < 4; i++) {
        EEPROM.get(addr, expo[i]); addr += sizeof(float);
    }
}

void eepromSave() {
    int addr = 1;
    for (int i = 0; i < 4; i++) {
        EEPROM.put(addr, calMin[i]);  addr += sizeof(int);
        EEPROM.put(addr, calMax[i]);  addr += sizeof(int);
        EEPROM.put(addr, calMid[i]);  addr += sizeof(int);
    }
    for (int i = 0; i < 10; i++) {
        EEPROM.put(addr, reversed[i]); addr += sizeof(bool);
    }
    for (int i = 0; i < 10; i++) {
        EEPROM.put(addr, trim[i]); addr += sizeof(int);
    }
    for (int i = 0; i < 4; i++) {
        EEPROM.put(addr, expo[i]); addr += sizeof(float);
    }
    EEPROM.commit();
}

const char PAGE_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Transmitter Config</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#0d0d0d;color:#e0e0e0;font-family:'Segoe UI',system-ui,sans-serif;padding:16px}
  h1{color:#4fc3f7;font-size:1.4rem;margin-bottom:20px;letter-spacing:1px}
  h2{color:#81d4fa;font-size:1rem;margin:24px 0 12px;border-bottom:1px solid #333;padding-bottom:6px}
  .card{background:#1a1a1a;border:1px solid #2a2a2a;border-radius:8px;padding:16px;margin-bottom:16px}
  .ch-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px}
  .ch-box{background:#111;border:1px solid #333;border-radius:6px;padding:10px;text-align:center}
  .ch-label{color:#81d4fa;font-size:0.75rem;margin-bottom:4px}
  .ch-val{color:#4fc3f7;font-size:1.3rem;font-weight:bold}
  .ch-bar{height:6px;background:#222;border-radius:3px;margin-top:6px;overflow:hidden}
  .ch-fill{height:100%;background:linear-gradient(90deg,#0288d1,#4fc3f7);border-radius:3px;transition:width 0.15s}
  table{width:100%;border-collapse:collapse;font-size:0.85rem}
  th{color:#81d4fa;text-align:left;padding:6px 8px;border-bottom:1px solid #333}
  td{padding:6px 8px;border-bottom:1px solid #1e1e1e;vertical-align:middle}
  input[type=number]{width:80px;background:#111;border:1px solid #444;color:#e0e0e0;padding:4px 6px;border-radius:4px}
  input[type=range]{width:120px;accent-color:#4fc3f7}
  .range-val{display:inline-block;width:40px;text-align:right;color:#4fc3f7;font-size:0.85rem}
  button{background:#0288d1;color:#fff;border:none;padding:8px 18px;border-radius:5px;cursor:pointer;font-size:0.85rem;margin-top:10px}
  button:hover{background:#039be5}
  button.sm{padding:4px 10px;margin:0;font-size:0.78rem}
  button.sm.act{background:#00897b}
  .toggle{position:relative;display:inline-block;width:40px;height:22px}
  .toggle input{opacity:0;width:0;height:0}
  .slider-tog{position:absolute;cursor:pointer;inset:0;background:#333;border-radius:11px;transition:.3s}
  .slider-tog:before{content:"";position:absolute;height:16px;width:16px;left:3px;top:3px;background:#aaa;border-radius:50%;transition:.3s}
  input:checked+.slider-tog{background:#0288d1}
  input:checked+.slider-tog:before{transform:translateX(18px);background:#fff}
  .toast{position:fixed;top:20px;right:20px;background:#00897b;color:#fff;padding:10px 20px;border-radius:6px;display:none;z-index:999;font-size:0.9rem;box-shadow:0 4px 12px rgba(0,0,0,.4)}
  .raw-adc{color:#888;font-size:0.75rem}
  .cal-row td{padding:4px 8px}
  .section-btn-row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:10px}
</style>
</head>
<body>
<div class="toast" id="toast">Saved</div>
<h1>Transmitter Config</h1>

<div class="card">
<h2>Live Channel Monitor</h2>
<div class="ch-grid" id="chGrid"></div>
</div>

<div class="card">
<h2>Calibration (CH1–CH4)</h2>
<table id="calTable">
<thead><tr><th>CH</th><th>Raw ADC</th><th>Min</th><th>Mid</th><th>Max</th><th>Action</th></tr></thead>
<tbody id="calBody"></tbody>
</table>
<div class="section-btn-row">
<button onclick="saveCalibration()">Save Calibration</button>
</div>
</div>

<div class="card">
<h2>Channel Reversal</h2>
<table>
<thead><tr><th>CH</th><th>Reversed</th></tr></thead>
<tbody id="revBody"></tbody>
</table>
<div class="section-btn-row">
<button onclick="saveReversal()">Save Reversal</button>
</div>
</div>

<div class="card">
<h2>Trim (all channels)</h2>
<table>
<thead><tr><th>CH</th><th>Trim (µs)</th><th>Value</th></tr></thead>
<tbody id="trimBody"></tbody>
</table>
<div class="section-btn-row">
<button onclick="saveTrim()">Save Trim</button>
</div>
</div>

<div class="card">
<h2>Expo (CH1–CH4)</h2>
<table>
<thead><tr><th>CH</th><th>Expo %</th><th>Value</th></tr></thead>
<tbody id="expoBody"></tbody>
</table>
<div class="section-btn-row">
<button onclick="saveExpo()">Save Expo</button>
</div>
</div>

<script>
var calMins=[200,200,200,200],calMaxs=[3895,3895,3895,3895],calMids=[2047,2047,2047,2047];
var rawAdcs=[0,0,0,0];
var calibrating=[false,false,false,false];

function buildChannelGrid(){
  var g=document.getElementById('chGrid');
  g.innerHTML='';
  for(var i=0;i<10;i++){
    var d=document.createElement('div');
    d.className='ch-box';
    d.id='chbox'+(i+1);
    d.innerHTML='<div class="ch-label">CH'+(i+1)+'</div><div class="ch-val" id="chv'+(i+1)+'">--</div><div class="ch-bar"><div class="ch-fill" id="chf'+(i+1)+'" style="width:50%"></div></div>';
    g.appendChild(d);
  }
}

function buildCalTable(){
  var tb=document.getElementById('calBody');
  tb.innerHTML='';
  for(var i=0;i<4;i++){
    var tr=document.createElement('tr');
    tr.className='cal-row';
    tr.innerHTML='<td>CH'+(i+1)+'</td>'
      +'<td><span class="raw-adc" id="radc'+(i+1)+'">--</span></td>'
      +'<td><input type="number" id="cmin'+(i+1)+'" value="'+calMins[i]+'" min="0" max="4095"></td>'
      +'<td><input type="number" id="cmid'+(i+1)+'" value="'+calMids[i]+'" min="0" max="4095"></td>'
      +'<td><input type="number" id="cmax'+(i+1)+'" value="'+calMaxs[i]+'" min="0" max="4095"></td>'
      +'<td>'
        +'<button class="sm" id="calBtn'+(i+1)+'" onclick="toggleCal('+(i+1)+')">Start Cal</button> '
        +'<button class="sm" onclick="setMid('+(i+1)+')">Set Mid</button>'
      +'</td>';
    tb.appendChild(tr);
  }
}

function buildRevTable(){
  var tb=document.getElementById('revBody');
  tb.innerHTML='';
  for(var i=0;i<10;i++){
    var tr=document.createElement('tr');
    tr.innerHTML='<td>CH'+(i+1)+'</td>'
      +'<td><label class="toggle"><input type="checkbox" id="rev'+(i+1)+'"><span class="slider-tog"></span></label></td>';
    tb.appendChild(tr);
  }
}

function buildTrimTable(){
  var tb=document.getElementById('trimBody');
  tb.innerHTML='';
  for(var i=0;i<10;i++){
    var tr=document.createElement('tr');
    tr.innerHTML='<td>CH'+(i+1)+'</td>'
      +'<td><input type="range" id="trim'+(i+1)+'" min="-100" max="100" value="0" oninput="document.getElementById(\'trimv'+(i+1)+'\').textContent=this.value"></td>'
      +'<td><span class="range-val" id="trimv'+(i+1)+'">0</span></td>';
    tb.appendChild(tr);
  }
}

function buildExpoTable(){
  var tb=document.getElementById('expoBody');
  tb.innerHTML='';
  for(var i=0;i<4;i++){
    var tr=document.createElement('tr');
    tr.innerHTML='<td>CH'+(i+1)+'</td>'
      +'<td><input type="range" id="expo'+(i+1)+'" min="0" max="100" value="0" oninput="document.getElementById(\'expov'+(i+1)+'\').textContent=this.value+\'%\'"></td>'
      +'<td><span class="range-val" id="expov'+(i+1)+'">0%</span></td>';
    tb.appendChild(tr);
  }
}

function showToast(){
  var t=document.getElementById('toast');
  t.style.display='block';
  setTimeout(function(){t.style.display='none';},2000);
}

function toggleCal(ch){
  var idx=ch-1;
  calibrating[idx]=!calibrating[idx];
  var btn=document.getElementById('calBtn'+ch);
  if(calibrating[idx]){
    btn.textContent='Stop Cal';
    btn.classList.add('act');
    calMins[idx]=4095;
    calMaxs[idx]=0;
  } else {
    btn.textContent='Start Cal';
    btn.classList.remove('act');
    document.getElementById('cmin'+ch).value=calMins[idx];
    document.getElementById('cmax'+ch).value=calMaxs[idx];
  }
}

function setMid(ch){
  var idx=ch-1;
  calMids[idx]=rawAdcs[idx];
  document.getElementById('cmid'+ch).value=calMids[idx];
}

function pollData(){
  fetch('/data').then(function(r){return r.json();}).then(function(d){
    for(var i=0;i<10;i++){
      var v=d.channels[i];
      document.getElementById('chv'+(i+1)).textContent=v;
      var pct=((v-1000)/1000*100).toFixed(1);
      document.getElementById('chf'+(i+1)).style.width=pct+'%';
    }
    for(var i=0;i<4;i++){
      rawAdcs[i]=d.raw[i];
      document.getElementById('radc'+(i+1)).textContent=d.raw[i];
      if(calibrating[i]){
        if(d.raw[i]<calMins[i]) calMins[i]=d.raw[i];
        if(d.raw[i]>calMaxs[i]) calMaxs[i]=d.raw[i];
      }
    }
    for(var i=0;i<10;i++){
      var rb=document.getElementById('rev'+(i+1));
      if(rb) rb.checked=d.reversed[i];
    }
    for(var i=0;i<10;i++){
      var ts=document.getElementById('trim'+(i+1));
      if(ts){
        ts.value=d.trim[i];
        document.getElementById('trimv'+(i+1)).textContent=d.trim[i];
      }
    }
    for(var i=0;i<4;i++){
      var es=document.getElementById('expo'+(i+1));
      if(es){
        var ep=Math.round(d.expo[i]*100);
        es.value=ep;
        document.getElementById('expov'+(i+1)).textContent=ep+'%';
      }
    }
    for(var i=0;i<4;i++){
      if(!calibrating[i]){
        calMins[i]=d.calMin[i];
        calMaxs[i]=d.calMax[i];
        calMids[i]=d.calMid[i];
        document.getElementById('cmin'+(i+1)).value=d.calMin[i];
        document.getElementById('cmid'+(i+1)).value=d.calMid[i];
        document.getElementById('cmax'+(i+1)).value=d.calMax[i];
      }
    }
  }).catch(function(){});
  setTimeout(pollData,200);
}

function saveCalibration(){
  var body={calMin:[],calMax:[],calMid:[]};
  for(var i=0;i<4;i++){
    body.calMin.push(parseInt(document.getElementById('cmin'+(i+1)).value)||200);
    body.calMid.push(parseInt(document.getElementById('cmid'+(i+1)).value)||2047);
    body.calMax.push(parseInt(document.getElementById('cmax'+(i+1)).value)||3895);
  }
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:'cal',data:body})})
    .then(function(r){if(r.ok)showToast();});
}

function saveReversal(){
  var body={reversed:[]};
  for(var i=0;i<10;i++){
    body.reversed.push(document.getElementById('rev'+(i+1)).checked);
  }
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:'reversed',data:body})})
    .then(function(r){if(r.ok)showToast();});
}

function saveTrim(){
  var body={trim:[]};
  for(var i=0;i<10;i++){
    body.trim.push(parseInt(document.getElementById('trim'+(i+1)).value)||0);
  }
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:'trim',data:body})})
    .then(function(r){if(r.ok)showToast();});
}

function saveExpo(){
  var body={expo:[]};
  for(var i=0;i<4;i++){
    body.expo.push(parseInt(document.getElementById('expo'+(i+1)).value)/100.0);
  }
  fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({type:'expo',data:body})})
    .then(function(r){if(r.ok)showToast();});
}

buildChannelGrid();
buildCalTable();
buildRevTable();
buildTrimTable();
buildExpoTable();
pollData();
</script>
</body>
</html>
)rawhtml";

void handleRoot() {
    server.send_P(200, "text/html", PAGE_HTML);
}

void handleData() {
    String json = "{";
    json += "\"channels\":[";
    for (int i = 0; i < 10; i++) {
        json += pkt.channels[i];
        if (i < 9) json += ",";
    }
    json += "],\"raw\":[";
    for (int i = 0; i < 4; i++) {
        json += rawADC[i];
        if (i < 3) json += ",";
    }
    json += "],\"calMin\":[";
    for (int i = 0; i < 4; i++) {
        json += calMin[i];
        if (i < 3) json += ",";
    }
    json += "],\"calMax\":[";
    for (int i = 0; i < 4; i++) {
        json += calMax[i];
        if (i < 3) json += ",";
    }
    json += "],\"calMid\":[";
    for (int i = 0; i < 4; i++) {
        json += calMid[i];
        if (i < 3) json += ",";
    }
    json += "],\"reversed\":[";
    for (int i = 0; i < 10; i++) {
        json += reversed[i] ? "true" : "false";
        if (i < 9) json += ",";
    }
    json += "],\"trim\":[";
    for (int i = 0; i < 10; i++) {
        json += trim[i];
        if (i < 9) json += ",";
    }
    json += "],\"expo\":[";
    for (int i = 0; i < 4; i++) {
        json += String(expo[i], 3);
        if (i < 3) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleSave() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Bad Request");
        return;
    }
    String body = server.arg("plain");

    if (body.indexOf("\"type\":\"cal\"") >= 0) {
        for (int i = 0; i < 4; i++) {
            String key;
            int pos;

            key = "\"calMin\":[";
            pos = body.indexOf(key);
            if (pos >= 0) {
                String sub = body.substring(pos + key.length());
                int vals[4];
                int cnt = 0;
                int start = 0;
                while (cnt < 4) {
                    int comma = sub.indexOf(',', start);
                    int bracket = sub.indexOf(']', start);
                    int end = (comma >= 0 && comma < bracket) ? comma : bracket;
                    vals[cnt++] = sub.substring(start, end).toInt();
                    if (end == bracket) break;
                    start = end + 1;
                }
                for (int j = 0; j < 4; j++) calMin[j] = vals[j];
            }

            key = "\"calMid\":[";
            pos = body.indexOf(key);
            if (pos >= 0) {
                String sub = body.substring(pos + key.length());
                int vals[4];
                int cnt = 0;
                int start = 0;
                while (cnt < 4) {
                    int comma = sub.indexOf(',', start);
                    int bracket = sub.indexOf(']', start);
                    int end = (comma >= 0 && comma < bracket) ? comma : bracket;
                    vals[cnt++] = sub.substring(start, end).toInt();
                    if (end == bracket) break;
                    start = end + 1;
                }
                for (int j = 0; j < 4; j++) calMid[j] = vals[j];
            }

            key = "\"calMax\":[";
            pos = body.indexOf(key);
            if (pos >= 0) {
                String sub = body.substring(pos + key.length());
                int vals[4];
                int cnt = 0;
                int start = 0;
                while (cnt < 4) {
                    int comma = sub.indexOf(',', start);
                    int bracket = sub.indexOf(']', start);
                    int end = (comma >= 0 && comma < bracket) ? comma : bracket;
                    vals[cnt++] = sub.substring(start, end).toInt();
                    if (end == bracket) break;
                    start = end + 1;
                }
                for (int j = 0; j < 4; j++) calMax[j] = vals[j];
            }
            break;
        }
        eepromSave();
        server.send(200, "text/plain", "OK");
        return;
    }

    if (body.indexOf("\"type\":\"reversed\"") >= 0) {
        String key = "\"reversed\":[";
        int pos = body.indexOf(key);
        if (pos >= 0) {
            String sub = body.substring(pos + key.length());
            int cnt = 0;
            int start = 0;
            while (cnt < 10) {
                int comma = sub.indexOf(',', start);
                int bracket = sub.indexOf(']', start);
                int end = (comma >= 0 && comma < bracket) ? comma : bracket;
                String tok = sub.substring(start, end);
                tok.trim();
                reversed[cnt++] = (tok == "true");
                if (end == bracket) break;
                start = end + 1;
            }
        }
        eepromSave();
        server.send(200, "text/plain", "OK");
        return;
    }

    if (body.indexOf("\"type\":\"trim\"") >= 0) {
        String key = "\"trim\":[";
        int pos = body.indexOf(key);
        if (pos >= 0) {
            String sub = body.substring(pos + key.length());
            int cnt = 0;
            int start = 0;
            while (cnt < 10) {
                int comma = sub.indexOf(',', start);
                int bracket = sub.indexOf(']', start);
                int end = (comma >= 0 && comma < bracket) ? comma : bracket;
                trim[cnt++] = sub.substring(start, end).toInt();
                if (end == bracket) break;
                start = end + 1;
            }
        }
        eepromSave();
        server.send(200, "text/plain", "OK");
        return;
    }

    if (body.indexOf("\"type\":\"expo\"") >= 0) {
        String key = "\"expo\":[";
        int pos = body.indexOf(key);
        if (pos >= 0) {
            String sub = body.substring(pos + key.length());
            int cnt = 0;
            int start = 0;
            while (cnt < 4) {
                int comma = sub.indexOf(',', start);
                int bracket = sub.indexOf(']', start);
                int end = (comma >= 0 && comma < bracket) ? comma : bracket;
                expo[cnt++] = sub.substring(start, end).toFloat();
                if (end == bracket) break;
                start = end + 1;
            }
        }
        eepromSave();
        server.send(200, "text/plain", "OK");
        return;
    }

    server.send(400, "text/plain", "Unknown type");
}

void handleNotFound() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

void initWiFiAP() {
    WiFi.softAP("Jatins Drone Transmitter", "JatinDixit");
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    dnsServer.start(DNS_PORT, "*", apIP);
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
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

    EEPROM.begin(EEPROM_SIZE);
    if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC) {
        Serial.println("First boot — writing EEPROM defaults");
        eepromWriteDefaults();
    }
    eepromLoad();

    WiFi.mode(WIFI_AP_STA);
    initWiFiAP();
    initEspNow();
    initLora();

    if (!loraReady && !espnowAvailable) {
        Serial.println("FATAL: no radio available");
    }
}

void loop() {
    static uint32_t lastTx = 0;
    static uint32_t lastPrint = 0;

    dnsServer.processNextRequest();
    server.handleClient();

    if (millis() - lastTx < 14) return;
    lastTx = millis();

    readChannels();

    if (millis() - lastPrint > 500) {
        lastPrint = millis();
        printChannels();
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
