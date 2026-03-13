#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============== 引脚定义 ==============
#define LL  2
#define UL  3
#define UR  4
#define LR  5
#define LED_PIN    6
#define BUTTON_PIN 7

// ============== 参数设置 ==============
#define MOTOR_STEP_DELAY 2000  // 电机每步延迟 (微秒)
#define STEP_PER_EVENT   15    // 每次触发旋转的步数 (约5度)
#define MAX_STEPS        360   // 120度对应的总步数 (120 * 3 = 360)
#define LONG_PRESS_MS    1500  // 长按判定时间 (毫秒)

// X27 电机节拍表
const uint8_t seq[4][4] = {
  {1,0,0,1},
  {0,1,0,1},
  {0,1,1,0},
  {1,0,1,0}
};

// ============== 状态变量 ==============
enum DeviceState {OFF, ON};
DeviceState deviceState = OFF; // 初始为关机状态

int currentStep = 0;           // 当前物理位置
int targetStep  = 0;           // 目标位置

bool ledFlashOnce = false;     // 收到信号闪烁标志
unsigned long flashStartTime = 0;
unsigned long lastLedToggle = 0;
bool ledToggleState = false;

unsigned long buttonPressStart = 0;
bool buttonWasDown = false;
bool longPressHandled = false;

// ============== 电机底层函数 ==============
void applyStep(int s) {
  digitalWrite(UL, seq[s][0]);
  digitalWrite(UR, seq[s][1]);
  digitalWrite(LL, seq[s][2]);
  digitalWrite(LR, seq[s][3]);
}

void motorOff() {
  digitalWrite(UL, LOW); digitalWrite(UR, LOW);
  digitalWrite(LL, LOW); digitalWrite(LR, LOW);
}

void stepForward() {
  currentStep++;
  applyStep(currentStep % 4);
  delayMicroseconds(MOTOR_STEP_DELAY);
}

void stepBackward() {
  currentStep--;
  applyStep(((currentStep % 4) + 4) % 4);
  delayMicroseconds(MOTOR_STEP_DELAY);
}

// 归零函数
void motorZero() {
  Serial.println("Resetting pointer to 0...");
  while(currentStep > 0) {
    stepBackward();
  }
  targetStep = 0;
  motorOff();
}

// ============== ESP-NOW 接收回调 ==============
void onDataRecv(const uint8_t * mac, const uint8_t *data, int len) {
  if (deviceState == OFF) return; // 关机状态不理会信号

  // 只要收到信号，目标位置增加
  if (targetStep < MAX_STEPS) {
    targetStep += 45; // 每次识别旋转15度 (15 * 3 = 45步)
    if (targetStep > MAX_STEPS) targetStep = MAX_STEPS;
    
    // 触发闪烁反馈
    ledFlashOnce = true;
    flashStartTime = millis();
    Serial.printf("Signal Received! New Target: %d\n", targetStep);
  }
}

// ============== LED 逻辑处理 ==============
void updateLED() {
  if (deviceState == OFF) {
    digitalWrite(LED_PIN, LOW); // 关机彻底灭灯
    return;
  }

  unsigned long now = millis();

  // 1. 如果到达 120度 (MAX_STEPS)，持续闪烁
  if (targetStep >= MAX_STEPS) {
    if (now - lastLedToggle > 200) {
      lastLedToggle = now;
      ledToggleState = !ledToggleState;
      digitalWrite(LED_PIN, ledToggleState);
    }
    return;
  }

  // 2. 收到信号时的短暂闪烁 (灭一下再亮)
  if (ledFlashOnce) {
    if (now - flashStartTime < 100) {
      digitalWrite(LED_PIN, LOW);
    } else {
      ledFlashOnce = false;
      digitalWrite(LED_PIN, HIGH);
    }
    return;
  }

  // 3. 正常开机状态常亮
  digitalWrite(LED_PIN, HIGH);
}

// ============== 按钮逻辑处理 ==============
void handleButton() {
  unsigned long now = millis();
  bool isDown = (digitalRead(BUTTON_PIN) == LOW);

  if (isDown && !buttonWasDown) {
    buttonPressStart = now;
    buttonWasDown = true;
    longPressHandled = false;
  }

  if (isDown && !longPressHandled) {
    // 检查是否长按 (开关机)
    if (now - buttonPressStart > LONG_PRESS_MS) {
      longPressHandled = true;
      if (deviceState == OFF) {
        deviceState = ON;
        Serial.println("Device POWER ON");
        motorZero(); // 开机归零
      } else {
        deviceState = OFF;
        Serial.println("Device POWER OFF");
        motorZero(); // 关机归零
        motorOff();
      }
    }
  }

  if (!isDown && buttonWasDown) {
    // 检查是否短按 (仅在开机时用于手动复位指针)
    if (!longPressHandled && deviceState == ON) {
      Serial.println("Manual Reset");
      motorZero();
    }
    buttonWasDown = false;
  }
}

// ============== 初始化 ==============
void setup() {
  Serial.begin(115200);
  
  pinMode(LL, OUTPUT); pinMode(UL, OUTPUT);
  pinMode(UR, OUTPUT); pinMode(LR, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  motorOff();
  digitalWrite(LED_PIN, LOW); // 初始关闭

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(esp_now_recv_cb_t(onDataRecv));
  }
  
  Serial.println("System Ready. LONG PRESS to start.");
}

// ============== 主循环 ==============
void loop() {
  handleButton();

  if (deviceState == ON) {
    // 让电机平滑移动到目标位置
    if (currentStep < targetStep) stepForward();
    else if (currentStep > targetStep) stepBackward();
    else motorOff(); // 到达位置后断电防热
  }

  updateLED();
  delay(1);
}