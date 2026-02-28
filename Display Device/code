#include <Arduino.h>

// ---- X27 wiring (按你说的物理位置) ----
#define LL 2   // 左下
#define UL 3   // 左上
#define UR 4   // 右上
#define LR 5   // 右下

#define LED_PIN 6
#define BUTTON_PIN 7

// 4-step sequence（和你之前能震动的版本同一种序列）
const uint8_t seq[4][4] = {
  {1, 0, 1, 0},
  {0, 1, 1, 0},
  {0, 1, 0, 1},
  {1, 0, 0, 1}
};

int stepIndex = 0;
bool running = false;

// 调速度：数值越大越慢（单位：微秒）
const unsigned long STEP_INTERVAL_US = 3000;
unsigned long lastStepUs = 0;

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

void stepForwardOnce() {
  stepIndex++;
  if (stepIndex > 3) stepIndex = 0;
  applyStep(stepIndex);
}

void setup() {
  pinMode(LL, OUTPUT);
  pinMode(UL, OUTPUT);
  pinMode(UR, OUTPUT);
  pinMode(LR, OUTPUT);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  motorOff();
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  // ---- Button toggle: 按下切换一次，并等待松手 ----
  if (digitalRead(BUTTON_PIN) == LOW) {
    running = !running;

    // 小延时去抖 + 等待松手（非常稳）
    delay(30);
    while (digitalRead(BUTTON_PIN) == LOW) {
      delay(5);
    }
  }

  // ---- Motor run/stop ----
  if (running) {
    digitalWrite(LED_PIN, HIGH);

    unsigned long now = micros();
    if (now - lastStepUs >= STEP_INTERVAL_US) {
      lastStepUs = now;
      stepForwardOnce();
    }
  } else {
    digitalWrite(LED_PIN, LOW);
    motorOff();
  }
}