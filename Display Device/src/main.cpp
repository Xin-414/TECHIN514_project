#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============== 引脚定义 ==============
#define LL  2   // 左下
#define UL  3   // 左上
#define UR  4   // 右上
#define LR  5   // 右下

#define LED_PIN     6
#define BUTTON_PIN  7

// ============== X27.168 步进电机参数 ==============
#define MOTOR_MAX_STEPS  945
#define MOTOR_STEP_DELAY 3000

const uint8_t seq[4][4] = {
    {1, 0, 1, 0},
    {0, 1, 1, 0},
    {0, 1, 0, 1},
    {1, 0, 0, 1}
};

int currentStep = 0;
int targetStep = 0;

// ============== 数据结构（必须与发送端一致）==============
typedef struct {
    uint16_t activityCount;
    uint16_t recentCount;
    float    activityRate;
    uint32_t lastActiveTime;
    bool     isActive;
} SensorData_t;

SensorData_t receivedData;
bool newDataReceived = false;
unsigned long lastDataTime = 0;

// ============== LED 动画 ==============
unsigned long lastLedToggle = 0;
bool ledState = false;
uint16_t ledBlinkInterval = 1000;

// ============== 按钮（重置计数）==============
unsigned long lastButtonPress = 0;
#define BUTTON_DEBOUNCE 300

// ============== ESP-NOW 回调 ==============
void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    Serial.printf("[ESP-NOW] Got %d bytes from %02X:%02X:%02X:%02X:%02X:%02X\n",
                  len, mac_addr[0], mac_addr[1], mac_addr[2],
                  mac_addr[3], mac_addr[4], mac_addr[5]);

    if (len == sizeof(SensorData_t)) {
        memcpy(&receivedData, data, sizeof(SensorData_t));
        newDataReceived = true;
        lastDataTime = millis();

        Serial.printf("[RECV] count=%d, recent=%d, rate=%.2f/min, active=%d\n",
                      receivedData.activityCount,
                      receivedData.recentCount,
                      receivedData.activityRate,
                      receivedData.isActive);
    } else {
        Serial.printf("[WARN] Expected %d bytes, got %d\n", sizeof(SensorData_t), len);
    }
}

// ============== X27.168 步进电机驱动 ==============
void applyStep(int s) {
    digitalWrite(UL, seq[s][0]);
    digitalWrite(UR, seq[s][1]);
    digitalWrite(LL, seq[s][2]);
    digitalWrite(LR, seq[s][3]);
}

void motorOff() {
    digitalWrite(UL, LOW);
    digitalWrite(UR, LOW);
    digitalWrite(LL, LOW);
    digitalWrite(LR, LOW);
}

void stepForward() {
    currentStep++;
    int s = currentStep % 4;
    applyStep(s);
    delayMicroseconds(MOTOR_STEP_DELAY);
}

void stepBackward() {
    currentStep--;
    int s = ((currentStep % 4) + 4) % 4;
    applyStep(s);
    delayMicroseconds(MOTOR_STEP_DELAY);
}

void motorZero() {
    Serial.println("Motor zeroing...");
    for (int i = 0; i < MOTOR_MAX_STEPS + 100; i++) {
        stepBackward();
    }
    currentStep = 0;
    motorOff();
    Serial.println("Motor zeroed");
}

void motorMoveTo(int target) {
    target = constrain(target, 0, MOTOR_MAX_STEPS);
    targetStep = target;
}

void motorUpdate() {
    if (currentStep < targetStep) {
        stepForward();
    } else if (currentStep > targetStep) {
        stepBackward();
    }
}

// ============== 数据映射到仪表位置 ==============
int mapDataToSteps(SensorData_t &data) {
    // 总次数 0-100 映射到全量程
    return map(constrain(data.activityCount, 0, 100),
               0, 100, 0, MOTOR_MAX_STEPS);
}

// ============== LED 更新 ==============
void updateLED(SensorData_t &data) {
    unsigned long now = millis();

    if (data.isActive) {
        ledBlinkInterval = 150;
    } else if (data.activityRate > 2.0) {
        ledBlinkInterval = 400;
    } else if (data.activityRate > 0.5) {
        ledBlinkInterval = 800;
    } else {
        digitalWrite(LED_PIN, LOW);
        ledState = false;
        return;
    }

    if (now - lastLedToggle >= ledBlinkInterval) {
        lastLedToggle = now;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
}

// ============== 按钮处理（重置计数）==============
void checkButton() {
    unsigned long now = millis();

    if (digitalRead(BUTTON_PIN) == LOW && (now - lastButtonPress > BUTTON_DEBOUNCE)) {
        lastButtonPress = now;

        // 重置计数
        receivedData.activityCount = 0;
        receivedData.recentCount = 0;
        receivedData.activityRate = 0;
        receivedData.isActive = false;

        // 指针归零
        motorMoveTo(0);

        Serial.println("[BUTTON] Counter reset!");

        // LED 闪三下确认
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(100);
            digitalWrite(LED_PIN, LOW);
            delay(100);
        }
    }
}

// ============== 连接状态检查 ==============
void checkConnection() {
    if (millis() - lastDataTime > 15000 && lastDataTime > 0) {
        motorMoveTo(0);
        unsigned long now = millis();
        if (now - lastLedToggle >= 2000) {
            lastLedToggle = now;
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
        }
    }
}

// ============== 初始化 ==============
void setup() {
    Serial.begin(115200);
    delay(3000);  // 等串口稳定
    Serial.println("\n=== Cat Activity Display (ESP32-C3) ===");

    // 引脚初始化
    pinMode(LL, OUTPUT);
    pinMode(UL, OUTPUT);
    pinMode(UR, OUTPUT);
    pinMode(LR, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // X27.168 归零
    motorZero();

    // ----- ESP-NOW 初始化 -----
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // 强制信道 1
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    // 打印 MAC 和信道
    Serial.print("Display MAC Address: ");
    Serial.println(WiFi.macAddress());
    uint8_t ch = 0;
    wifi_second_chan_t sch;
    esp_wifi_get_channel(&ch, &sch);
    Serial.printf("WiFi Channel: %d\n", ch);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESP-NOW init failed!");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);

    Serial.println("ESP-NOW initialized, waiting for data...\n");

    // 开机动画
    motorMoveTo(MOTOR_MAX_STEPS / 2);
    while (currentStep != targetStep) motorUpdate();
    delay(300);
    motorMoveTo(0);
    while (currentStep != targetStep) motorUpdate();
    delay(500);

    Serial.println("Display ready!\n");
}

// ============== 主循环 ==============
void loop() {
    checkButton();

    if (newDataReceived) {
        newDataReceived = false;
        int newTarget = mapDataToSteps(receivedData);
        motorMoveTo(newTarget);
        Serial.printf("[GAUGE] Target step: %d / %d\n", newTarget, MOTOR_MAX_STEPS);
    }

    motorUpdate();

    if (lastDataTime > 0) {
        updateLED(receivedData);
    }

    checkConnection();

    delay(1);
}