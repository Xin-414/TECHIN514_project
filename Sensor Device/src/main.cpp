#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============== 引脚定义 ==============
#define PIR_PIN          2       // PIR 传感器信号引脚（你实际接线）

// ============== 参数配置 ==============
#define DEBOUNCE_MS      500     // PIR 去抖时间（毫秒）
#define SEND_INTERVAL_MS 5000    // 定时发送间隔（毫秒）

// ============== 数据结构 ==============
// 发送给 Display Device 的数据包
typedef struct {
    uint16_t activityCount;    // 累计活动次数
    uint16_t recentCount;      // 最近一段时间的活动次数（用于DSP/ML）
    float    activityRate;     // 活动频率（次/分钟）
    uint32_t lastActiveTime;   // 最后一次活动的时间戳(ms)
    bool     isActive;         // 当前是否检测到活动
} SensorData_t;

SensorData_t sensorData;

// ============== ESP-NOW 配置 ==============
// !! 重要：把这里换成你 Display Device (ESP32-C3) 的 MAC 地址 !!
// 先烧录 display device 的代码，串口会打印它的 MAC 地址
uint8_t displayDeviceMAC[] = {0x98, 0x3D, 0xAE, 0xAC, 0xAC, 0xF4};

// ESP-NOW 发送状态
bool lastSendSuccess = false;

// ============== 活动检测变量 ==============
volatile uint16_t totalActivityCount = 0;
uint16_t recentActivityCount = 0;
unsigned long lastTriggerTime = 0;
unsigned long lastSendTime = 0;
unsigned long windowStartTime = 0;
int lastPirState = LOW;  // 用于边沿检测

// 滑动窗口用于计算频率（简单DSP）
#define WINDOW_SIZE 12           // 记录最近12次触发
unsigned long triggerTimestamps[WINDOW_SIZE];
uint8_t triggerIndex = 0;
uint8_t triggerCount = 0;

// ============== 回调函数 ==============
// ESP-NOW 发送完成回调
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
    Serial.print("Send status: ");
    Serial.println(lastSendSuccess ? "SUCCESS" : "FAIL");
}

// ============== 活动频率计算（DSP部分）==============
float calculateActivityRate() {
    if (triggerCount < 2) return 0.0;

    // 找到窗口中最早和最晚的时间戳
    unsigned long earliest = triggerTimestamps[0];
    unsigned long latest = triggerTimestamps[0];

    for (uint8_t i = 0; i < triggerCount; i++) {
        if (triggerTimestamps[i] < earliest) earliest = triggerTimestamps[i];
        if (triggerTimestamps[i] > latest) latest = triggerTimestamps[i];
    }

    unsigned long duration = latest - earliest;
    if (duration == 0) return 0.0;

    // 返回 次/分钟
    return (float)(triggerCount - 1) / ((float)duration / 60000.0);
}

void recordTrigger(unsigned long timestamp) {
    triggerTimestamps[triggerIndex] = timestamp;
    triggerIndex = (triggerIndex + 1) % WINDOW_SIZE;
    if (triggerCount < WINDOW_SIZE) triggerCount++;
}

// ============== 初始化 ==============
void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("\n=== Cat Activity Sensor (ESP32-S3) ===");

    // 引脚初始化
    pinMode(PIR_PIN, INPUT);

    // 初始化数据
    memset(&sensorData, 0, sizeof(sensorData));
    memset(triggerTimestamps, 0, sizeof(triggerTimestamps));
    windowStartTime = millis();

    // ----- ESP-NOW 初始化 -----
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // 强制 WiFi 协议为 802.11 b/g/n（兼容 C3）
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    // 强制设到信道 1
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    // 打印自己的 MAC 地址和信道（调试用）
    Serial.print("Sensor MAC Address: ");
    Serial.println(WiFi.macAddress());
    uint8_t primaryChan = 0;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&primaryChan, &secondChan);
    Serial.printf("WiFi Channel: %d\n", primaryChan);

    // Step 2: 初始化 ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESP-NOW init failed!");
        return;
    }
    Serial.println("ESP-NOW initialized");

    // Step 3: 注册发送回调
    esp_now_register_send_cb(onDataSent);

    // Step 4: 添加广播 peer（先用广播确认通信）
    esp_now_peer_info_t peerInfo = {};
    // 用广播地址 FF:FF:FF:FF:FF:FF 测试
    memset(peerInfo.peer_addr, 0xFF, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("ERROR: Failed to add peer");
        return;
    }
    Serial.println("Broadcast peer added");
    Serial.println("Waiting for PIR sensor to stabilize...");

    // PIR 传感器需要预热时间
    delay(5000);  // 实际部署建议30秒，调试时可以缩短
    Serial.println("Sensor ready!\n");
}

// ============== 发送数据 ==============
void sendData() {
    // 打包数据
    sensorData.activityCount = totalActivityCount;
    sensorData.recentCount = recentActivityCount;
    sensorData.activityRate = calculateActivityRate();
    sensorData.lastActiveTime = lastTriggerTime;
    sensorData.isActive = digitalRead(PIR_PIN);

    // 发送（广播给所有 peer）
    uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t result = esp_now_send(broadcastMAC,
                                     (uint8_t *)&sensorData,
                                     sizeof(sensorData));

    if (result == ESP_OK) {
        Serial.printf("[SEND] count=%d, recent=%d, rate=%.2f/min\n",
                      sensorData.activityCount,
                      sensorData.recentCount,
                      sensorData.activityRate);
    } else {
        Serial.println("[SEND] Error sending data");
    }

    // 重置 recent count（每次发送后清零）
    recentActivityCount = 0;
}

// ============== 主循环 ==============
void loop() {
    unsigned long now = millis();

    // ----- 读取 PIR 传感器（边沿检测）-----
    int pirState = digitalRead(PIR_PIN);

    // 上升沿：从 LOW 变 HIGH 才算一次触发
    if (pirState == HIGH && lastPirState == LOW && (now - lastTriggerTime > DEBOUNCE_MS)) {
        // 检测到新的活动
        totalActivityCount++;
        recentActivityCount++;
        lastTriggerTime = now;

        // 记录到滑动窗口
        recordTrigger(now);

        // LED 闪一下表示检测到
        Serial.printf("[PIR] Activity #%d detected! Rate: %.2f/min\n",
                      totalActivityCount, calculateActivityRate());

        // 立即发送一次（实时通知）
        sendData();

        delay(200);
    }

    lastPirState = pirState;

    // ----- 定时发送（心跳包）-----
    if (now - lastSendTime >= SEND_INTERVAL_MS) {
        lastSendTime = now;
        Serial.printf("PIR=%d\n", digitalRead(PIR_PIN));
        sendData();
    }

    delay(50);  // 与你原来测试代码一致
}


