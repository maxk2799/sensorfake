#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLECharacteristic *pCharacteristic;
BLECharacteristic *pRscChar;
bool deviceConnected = false;

unsigned long previousMillis = 0;
const long updateInterval = 1000;

float hr = 100.0;
float lastHr = 100.0;
float startHr = 100.0;
float warmupPeak = 145.0;
float tempoStart = 168.0;
float tempoEnd = 174.0;
float sessionMax = 182.0;
float micro = 0.0;
int sessionSeconds = 0;
int warmupEnd = 360;
int buildEnd = 600;
int effortEnd = 1800;
int peakAt = 1200;
int peakLen = 10;
int spikeAge = 0;
int spikeLen = 0;
float spikeAmp = 0.0;
int spikeCooldown = 0;
int sameStreak = 0;
uint8_t lastBroadcast = 100;
float speedMps = 2.1;
float lastSpeed = 2.1;
uint32_t distanceTenths = 0;

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

float smoothstep(float t) {
  t = clampf(t, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

float hrToSpeed(float bpm) {
  if (bpm < 100.0) return clampf(1.50 + (bpm - 88.0) * 0.045, 0.0, 2.05);
  if (bpm < 110.0) return 2.05 + (bpm - 100.0) * 0.017;
  if (bpm < 145.0) return 2.22 + (bpm - 110.0) * 0.0137;
  if (bpm < 170.0) return 2.70 + (bpm - 145.0) * 0.0232;
  return 3.28 + (bpm - 170.0) * 0.0193;
}

void startNewSession() {
  randomSeed(esp_random() ^ (uint32_t)millis() ^ (uint32_t)micros());

  startHr = random(90, 111);
  warmupPeak = random(140, 152);
  tempoStart = random(168, 171);
  tempoEnd = random(172, 176);
  sessionMax = random(180, 185);

  warmupEnd = random(300, 481);
  buildEnd = warmupEnd + random(180, 331);
  effortEnd = buildEnd + random(900, 1801);
  peakAt = buildEnd + random(180, (effortEnd - buildEnd) - 90);
  peakLen = random(10, 18);

  hr = startHr;
  lastHr = startHr;
  micro = 0.0;
  sessionSeconds = 0;
  spikeAge = 0;
  spikeLen = 0;
  spikeAmp = 0.0;
  spikeCooldown = random(45, 100);
  sameStreak = 0;
  lastBroadcast = (uint8_t)(startHr + 0.5);
  speedMps = hrToSpeed(startHr);
  lastSpeed = speedMps;
  distanceTenths = 0;
  previousMillis = 0;

  Serial.print("New session | start=");
  Serial.print((int)startHr);
  Serial.print(" tempo=");
  Serial.print((int)tempoStart);
  Serial.print("-");
  Serial.print((int)tempoEnd);
  Serial.print(" max=");
  Serial.println((int)sessionMax);
}

#define heartRateService BLEUUID((uint16_t)0x180D)
#define rscService BLEUUID((uint16_t)0x1814)
BLEUUID heartRateCharUUID((uint16_t)0x2A37);
BLEUUID rscMeasUUID((uint16_t)0x2A53);
BLEUUID rscFeatUUID((uint16_t)0x2A54);

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

  BLEService *pRsc = pServer->createService(rscService);
  pRscChar = pRsc->createCharacteristic(
               rscMeasUUID,
               BLECharacteristic::PROPERTY_NOTIFY
             );
  pRscChar->addDescriptor(new BLE2902());
  BLECharacteristic *pRscFeat = pRsc->createCharacteristic(
                                  rscFeatUUID,
                                  BLECharacteristic::PROPERTY_READ
                                );
  uint8_t rscFeature[2] = {0x06, 0x00};
  pRscFeat->setValue(rscFeature, 2);
  pRsc->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(heartRateService);
  pAdvertising->addServiceUUID(rscService);
  pServer->getAdvertising()->start();

  Serial.println("Broadcasting fake Polar device...");
}

void loop() {
  if (deviceConnected) {
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= updateInterval) {
      previousMillis = currentMillis;
      sessionSeconds++;

      float baseline = startHr;
      bool inPeak = false;

      if (sessionSeconds <= warmupEnd) {
        float t = (float)sessionSeconds / (float)warmupEnd;
        baseline = startHr + (warmupPeak - startHr) * smoothstep(t);
      } else if (sessionSeconds <= buildEnd) {
        float t = (float)(sessionSeconds - warmupEnd) / (float)(buildEnd - warmupEnd);
        baseline = warmupPeak + (tempoStart - warmupPeak) * smoothstep(t);
      } else if (sessionSeconds <= effortEnd) {
        float tempoLen = (float)(effortEnd - buildEnd);
        float t = (float)(sessionSeconds - buildEnd) / tempoLen;
        baseline = tempoStart + (tempoEnd - tempoStart) * t;

        if (sessionSeconds >= peakAt && sessionSeconds < peakAt + peakLen) {
          inPeak = true;
          float pt = (float)(sessionSeconds - peakAt) / (float)peakLen;
          float peakLift = sessionMax - baseline;
          if (pt < 0.4) baseline += peakLift * (pt / 0.4);
          else baseline += peakLift * (1.0 - ((pt - 0.4) / 0.6));
        }

        if (!inPeak) {
          if (spikeCooldown > 0) spikeCooldown--;
          if (spikeLen == 0 && spikeCooldown == 0 && random(0, 100) < 4) {
            spikeAmp = random(4, 9);
            spikeLen = random(10, 21);
            spikeAge = 0;
            spikeCooldown = spikeLen + random(50, 140);
          }
          if (spikeLen > 0) {
            spikeAge++;
            float st = (float)spikeAge / (float)spikeLen;
            if (st < 0.4) baseline += spikeAmp * (st / 0.4);
            else baseline += spikeAmp * (1.0 - ((st - 0.4) / 0.6));
            if (spikeAge >= spikeLen) {
              spikeLen = 0;
              spikeAge = 0;
              spikeAmp = 0.0;
            }
          }
        }
      } else {
        int recovered = sessionSeconds - effortEnd;
        if (recovered < 75) baseline = lastHr - (0.55 + random(0, 20) / 100.0);
        else if (recovered < 180) baseline = lastHr - (0.22 + random(0, 12) / 100.0);
        else baseline = lastHr - (0.10 + random(0, 8) / 100.0);
        if (baseline < 88.0) baseline = 88.0 + random(0, 4);
      }

      micro += random(-12, 13) / 10.0;
      micro = clampf(micro, -3.0, 3.0);

      float target = baseline + micro;
      if (sessionSeconds > effortEnd) {
        target = baseline + clampf(micro, -1.2, 0.4);
        if (target > lastHr - 0.08 && lastHr > 92.0) target = lastHr - 0.12;
      }
      if (!inPeak) target = clampf(target, 80.0, sessionMax - 4.0);
      else target = clampf(target, 80.0, sessionMax);

      float maxStep = inPeak ? 2.5 : 1.6;
      if (sessionSeconds > effortEnd) maxStep = 0.9;
      float delta = target - lastHr;
      if (delta > maxStep) target = lastHr + maxStep;
      if (delta < -maxStep) target = lastHr - maxStep;

      hr = target;
      lastHr = hr;

      uint8_t broadcastHR = (uint8_t)(hr + 0.5);
      if (broadcastHR == lastBroadcast) {
        sameStreak++;
        if (sameStreak >= 1) {
          int dir = (micro >= 0.0) ? 1 : -1;
          int next = (int)broadcastHR + dir;
          if (next < 80 || next > (int)sessionMax || next == (int)broadcastHR) {
            next = (int)broadcastHR - dir;
          }
          next = (int)clampf((float)next, 80.0, sessionMax);
          if (next == (int)lastBroadcast) next = (int)lastBroadcast + (dir == 1 ? -1 : 1);
          broadcastHR = (uint8_t)clampf((float)next, 80.0, sessionMax);
          sameStreak = 0;
        }
      } else {
        sameStreak = 0;
      }
      lastBroadcast = broadcastHR;

      float targetSpeed = 0.0;
      float maxSpeedStep = 0.07;
      if (sessionSeconds <= effortEnd) {
        targetSpeed = hrToSpeed((float)broadcastHR);
        maxSpeedStep = 0.08;
      } else {
        targetSpeed = 0.0;
        maxSpeedStep = 0.30;
      }
      float sdelta = targetSpeed - lastSpeed;
      if (sdelta > maxSpeedStep) targetSpeed = lastSpeed + maxSpeedStep;
      if (sdelta < -maxSpeedStep) targetSpeed = lastSpeed - maxSpeedStep;
      if (sessionSeconds > effortEnd && targetSpeed < 0.25) targetSpeed = 0.0;
      speedMps = clampf(targetSpeed, 0.0, 4.2);
      lastSpeed = speedMps;
      if (speedMps > 0.2) distanceTenths += (uint32_t)(speedMps * 10.0 + 0.5);

      uint8_t cadence = 0;
      if (speedMps > 0.5) {
        cadence = (uint8_t)clampf(74.0 + speedMps * 6.2 + random(-1, 2), 70.0, 96.0);
      }
      bool running = speedMps >= 2.05;
      uint16_t spd256 = (uint16_t)(speedMps * 256.0 + 0.5);
      uint8_t rscFlags = 0x02;
      if (running) rscFlags |= 0x04;
      uint8_t rscData[8] = {
        rscFlags,
        (uint8_t)(spd256 & 0xFF),
        (uint8_t)(spd256 >> 8),
        cadence,
        (uint8_t)(distanceTenths & 0xFF),
        (uint8_t)((distanceTenths >> 8) & 0xFF),
        (uint8_t)((distanceTenths >> 16) & 0xFF),
        (uint8_t)((distanceTenths >> 24) & 0xFF)
      };

      uint8_t hrmData[2] = {0b00000110, broadcastHR};
      pCharacteristic->setValue(hrmData, 2);
      pCharacteristic->notify();
      pRscChar->setValue(rscData, 8);
      pRscChar->notify();

      Serial.print("HR: ");
      Serial.print(broadcastHR);
      if (speedMps < 0.2) {
        Serial.println(" | pace: --");
      } else {
        int paceSec = (int)(1000.0 / speedMps + 0.5);
        Serial.print(" | pace: ");
        Serial.print(paceSec / 60);
        Serial.print(":");
        int secs = paceSec % 60;
        if (secs < 10) Serial.print("0");
        Serial.println(secs);
      }
    }
  }
}
