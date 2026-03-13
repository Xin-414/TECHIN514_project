#include <esp_now.h>
#include <WiFi.h>

// 1. Target MAC Address (Your C3 Address)
uint8_t receiverAddress[] = {0x58, 0x8C, 0x81, 0x9F, 0xC2, 0xD0};

// 2. Define PIR Pin
#define PIR_PIN 2

// 3. Variable to store signal
int motionDetected = 1; 

void setup() {
  Serial.begin(115200);

  // 4. Initialize PIR Pin
  pinMode(PIR_PIN, INPUT);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("S3 Sensor with PIR ready!");
}

void loop() {
  // 5. Read PIR sensor
  int pState = digitalRead(PIR_PIN);

  if (pState == HIGH) {
    // 6. If motion detected, send signal to C3
    Serial.println("Motion Detected! Sending signal...");
    esp_now_send(receiverAddress, (uint8_t *) &motionDetected, sizeof(motionDetected));
    
    // 7. Wait a bit to avoid sending too many signals for one movement
    // PIR sensors usually stay HIGH for a few seconds anyway
    delay(2000); 
  }
}