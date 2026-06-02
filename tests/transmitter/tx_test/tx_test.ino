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

int calMin[4] = {4095, 4095, 4095, 4095};
int calMax[4] = {0, 0, 0, 0};

int readAvg(int pin) {
    int s = 0;
    for (int i = 0; i < 8; i++) s += analogRead(pin);
    return s / 8;
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
}

void loop() {
    int r1 = readAvg(PIN_CH1);
    int r2 = readAvg(PIN_CH2);
    int r3 = readAvg(PIN_CH3);
    int r4 = readAvg(PIN_CH4);

    for (int i = 0; i < 4; i++) {
        int vals[4] = {r1, r2, r3, r4};
        if (vals[i] < calMin[i]) calMin[i] = vals[i];
        if (vals[i] > calMax[i]) calMax[i] = vals[i];
    }

    Serial.println("CH       RAW    MIN    MAX");
    Serial.println("--------------------------");
    Serial.print("CH1   "); Serial.print("  "); Serial.print(r1);  Serial.print("   "); Serial.print(calMin[0]); Serial.print("   "); Serial.println(calMax[0]);
    Serial.print("CH2   "); Serial.print("  "); Serial.print(r2);  Serial.print("   "); Serial.print(calMin[1]); Serial.print("   "); Serial.println(calMax[1]);
    Serial.print("CH3   "); Serial.print("  "); Serial.print(r3);  Serial.print("   "); Serial.print(calMin[2]); Serial.print("   "); Serial.println(calMax[2]);
    Serial.print("CH4   "); Serial.print("  "); Serial.print(r4);  Serial.print("   "); Serial.print(calMin[3]); Serial.print("   "); Serial.println(calMax[3]);
    Serial.println();
    Serial.println("CH       STATE");
    Serial.println("--------------");
    Serial.print("CH5   "); Serial.println(digitalRead(PIN_CH5)  ? "HIGH" : "LOW");
    Serial.print("CH6   "); Serial.println(digitalRead(PIN_CH6)  ? "HIGH" : "LOW");
    Serial.print("CH7   "); Serial.println(digitalRead(PIN_CH7)  ? "HIGH" : "LOW");
    Serial.print("CH8   "); Serial.println(digitalRead(PIN_CH8)  ? "HIGH" : "LOW");
    Serial.print("CH9   "); Serial.println(digitalRead(PIN_CH9)  ? "HIGH" : "LOW");
    Serial.print("CH10  "); Serial.println(digitalRead(PIN_CH10) ? "HIGH" : "LOW");
    Serial.println("==========================");

    delay(300);
}
