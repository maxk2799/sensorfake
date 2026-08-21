#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

unsigned long previousMillis = 0;
const long updateInterval = 1000;

float trend = 62.0;
int sessionSeconds = 0;
int warmupSeconds = 720;
float warmupTarget = 155.0;
int holdLeft = 0;
int segmentKind = 0;
int segmentLeft = 0;
float segmentRate = 0.1;
int bumpAge = 0;
int bumpLen = 0;
float bumpPeak = 0.0;
int bumpCooldown = 0;
int warmupHoldChance = 7;
int bumpChance = 5;
int bumpPeakMin = 6;
int bumpPeakMax = 24;
int bumpGapMin = 30;
int bumpGapMax = 150;

void startNewSession() {
  randomSeed(esp_random() ^ (uint32_t)millis() ^ (uint32_t)micros());
  trend = random(55, 72);
  warmupSeconds = random(480, 961);
  warmupTarget = random(148, 169);
  sessionSeconds = 0;
  holdLeft = 0;
  segmentKind = 0;
  segmentLeft = 0;
  segmentRate = random(3, 16) / 100.0;
  bumpAge = 0;
  bumpLen = 0;
  bumpPeak = 0.0;
  bumpCooldown = random(8, 45);
  warmupHoldChance = random(4, 12);
  bumpChance = random(3, 9);
  bumpPeakMin = random(5, 10);
  bumpPeakMax = random(16, 28);
  bumpGapMin = random(20, 50);
  bumpGapMax = random(90, 180);
  previousMillis = 0;

  Serial.print("New session | start=");
  Serial.print((int)trend);
  Serial.print(" warmup=");
  Serial.print(warmupSeconds);
  Serial.print("s target=");
  Serial.println((int)warmupTarget);
}

#define heartRateService BLEUUID((uint16_t)0x180D)
BLEUUID heartRateCharUUID((uint16_t)0x2A37);

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      startNewSession();
      Serial.println("App connected!");
    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("App disconnected. Advertising again...");
      pServer->getAdvertising()->start();
    }
};

void setup() {
  Serial.begin(115200);
  startNewSession();

  BLEDevice::init("Polar Sense 1234567"); 
  
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(heartRateService);
  
  pCharacteristic = pService->createCharacteristic(
                      heartRateCharUUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
                    
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(heartRateService);
  pServer->getAdvertising()->start();
  
  Serial.println("Broadcasting fake Polar device...");
}

void loop() {
  if (deviceConnected) {
    unsigned long currentMillis = millis();

    // Only update once per second
    if (currentMillis - previousMillis >= updateInterval) {
      previousMillis = currentMillis;

      sessionSeconds++;

      if (sessionSeconds <= warmupSeconds) {
        if (holdLeft > 0) {
          holdLeft--;
          trend += random(-8, 9) / 100.0;
        } else if (random(0, 100) < warmupHoldChance) {
          holdLeft = random(5, 18);
        } else {
          float remaining = (float)(warmupSeconds - sessionSeconds + 1);
          float gap = warmupTarget - trend;
          float rate = (gap / remaining) * (random(90, 170) / 100.0);
          if (rate < 0.06) rate = 0.06;
          if (rate > 0.42) rate = 0.42;
          if (gap < 0.4) rate = random(-12, 13) / 100.0;
          trend += rate;
          if (random(0, 100) < 2) trend -= random(6, 18) / 10.0;
        }
      } else {
        if (segmentLeft <= 0) {
          int r = random(0, 100);
          if (segmentKind == 2) {
            if (r < 55) segmentKind = 0;
            else if (r < 92) segmentKind = 1;
            else segmentKind = 2;
          } else {
            if (r < 48) segmentKind = 0;
            else if (r < 78) segmentKind = 1;
            else segmentKind = 2;
          }
          if (segmentKind == 0) segmentLeft = random(28, 150);
          else if (segmentKind == 1) segmentLeft = random(16, 70);
          else segmentLeft = random(10, 42);
          segmentRate = random(3, 16) / 100.0;
        }
        segmentLeft--;
        if (segmentKind == 0) {
          trend += random(-14, 15) / 100.0;
        } else if (segmentKind == 1) {
          trend += segmentRate + random(-10, 11) / 100.0;
        } else {
          trend -= segmentRate + random(-10, 11) / 100.0;
        }
        if (trend > warmupTarget + 24.0) {
          trend = warmupTarget + 24.0;
          segmentKind = 0;
          segmentLeft = random(20, 80);
        } else if (trend < warmupTarget - 22.0) {
          trend = warmupTarget - 22.0;
          segmentKind = 0;
          segmentLeft = random(20, 80);
        }
      }

      if (bumpCooldown > 0) bumpCooldown--;
      if (bumpLen == 0 && bumpCooldown == 0 && random(0, 100) < bumpChance) {
        bumpPeak = random(bumpPeakMin, bumpPeakMax);
        bumpLen = random(5, 18);
        bumpAge = 0;
        bumpCooldown = bumpLen + random(bumpGapMin, bumpGapMax);
      }

      float bump = 0.0;
      if (bumpLen > 0) {
        bumpAge++;
        float t = (float)bumpAge / (float)bumpLen;
        if (t < 0.35) bump = bumpPeak * (t / 0.35);
        else bump = bumpPeak * (1.0 - ((t - 0.35) / 0.65));
        if (bumpAge >= bumpLen) {
          bumpLen = 0;
          bumpAge = 0;
          bumpPeak = 0.0;
        }
      }

      if (trend < 50.0) trend = 50.0;
      if (trend > 188.0) trend = 188.0;

      float jitter = random(-22, 23) / 10.0;
      float hr = trend + bump + jitter;
      if (hr < 48.0) hr = 48.0;
      if (hr > 196.0) hr = 196.0;

      uint8_t broadcastHR = (uint8_t)(hr + 0.5);

      uint8_t hrmData[2] = {0b00000110, broadcastHR};
      pCharacteristic->setValue(hrmData, 2);
      pCharacteristic->notify();

      Serial.print("Simulating HR: ");
      Serial.println(broadcastHR);
    }
  }
}