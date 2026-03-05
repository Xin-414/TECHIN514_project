#include <Arduino.h>

#define PIR_PIN 3

#define MAX_RECORDS 100
unsigned long detections[MAX_RECORDS];
int detectIndex = 0;
int detectCount = 0;

int lastState = LOW;
bool alerted = false;
int totalDetections = 0;

void setup() {
  pinMode(PIR_PIN, INPUT);
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== PIR Sensor Ready ===");
  Serial.println("Monitoring started...");
}

void loop() {
  int motion = digitalRead(PIR_PIN);
  unsigned long now = millis();

  if (motion == HIGH && lastState == LOW) {
    totalDetections++;
    Serial.print("[Detected] #");
    Serial.print(totalDetections);
    Serial.print(" at ");
    Serial.print(now / 1000);
    Serial.println("s");

    detections[detectIndex] = now;
    detectIndex = (detectIndex + 1) % MAX_RECORDS;
    if (detectCount < MAX_RECORDS) detectCount++;

    int countInLastMinute = 0;
    for (int i = 0; i < detectCount; i++) {
      if (now - detections[i] <= 60000) {
        countInLastMinute++;
      }
    }

    Serial.print("  -> Last 60s: ");
    Serial.print(countInLastMinute);
    Serial.println(" times");

    if (countInLastMinute >= 10 && !alerted) {
      Serial.println("!!! ALERT: Over 10 detections in 1 minute !!!");
      alerted = true;
    }
  }

  if (motion == LOW && lastState == HIGH) {
    Serial.print("[Clear] Motion ended at ");
    Serial.print(now / 1000);
    Serial.println("s");
  }

  if (alerted) {
    int recent = 0;
    for (int i = 0; i < detectCount; i++) {
      if (now - detections[i] <= 60000) {
        recent++;
      }
    }
    if (recent < 10) {
      alerted = false;
      Serial.println("[INFO] Alert cleared.");
    }
  }

  lastState = motion;
  delay(50);
}