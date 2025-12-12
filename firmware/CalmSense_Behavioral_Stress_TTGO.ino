#include <WiFi.h>
#include "ThingSpeak.h"
#include <Wire.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_LSM6DSO.h>

// -------------------- TEMP/HUMIDITY OPTIONS --------------------
// Uncomment exactly ONE of these options based on your sensor.
// If you are unsure, try DHT first. If it fails, try SHT31.

// ---- Option A: DHT11 or DHT22 ----
#define USE_DHT 1
// #define USE_SHT31 1

#if defined(USE_DHT)
  #include <DHT.h>
  // Common DHT wiring: DATA pin to a digital GPIO
  const int PIN_DHT = 26;       // change if needed
  #define DHTTYPE DHT11         // change to DHT22 if you have DHT22
  DHT dht(PIN_DHT, DHTTYPE);
#endif

// ---- Option B: SHT31 (I2C) ----
#if defined(USE_SHT31)
  #include <Adafruit_SHT31.h>
  Adafruit_SHT31 sht31 = Adafruit_SHT31();
#endif

// -------------------- WIFI + THINGSPEAK --------------------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

WiFiClient client;

unsigned long TS_CHANNEL_ID = 1234567;           // your channel ID
const char* TS_WRITE_API_KEY = "XXXXXXXXXXXXXX"; // your write key

// ThingSpeak Fields (create 4 fields):
// Field1: MotionScore (0-100)
// Field2: EnvScore (0-100)
// Field3: TouchRate (touches/min)
// Field4: StressScore (0-100)

// -------------------- PINS --------------------
const int PIN_TOUCH = 27;   // touch sensor digital OUT
const int PIN_LIGHT = 34;   // light sensor analog OUT (ADC)
const int PIN_BUZZ  = 25;   // buzzer
const int PIN_LED_G = 13;   // green LED
const int PIN_LED_Y = 12;   // yellow LED
const int PIN_LED_R = 14;   // red LED

// -------------------- LSM6DSO (I2C) --------------------
Adafruit_LSM6DSO lsm6dso;

// -------------------- TIMING --------------------
unsigned long lastUploadMs = 0;
const unsigned long UPLOAD_INTERVAL_MS = 15000;

unsigned long baselineStartMs = 0;
const unsigned long BASELINE_DURATION_MS = 30000;
bool baselineReady = false;

// -------------------- TOUCH COUNTING --------------------
volatile unsigned long touchCount = 0;
unsigned long touchWindowStartMs = 0;

void IRAM_ATTR onTouchRising() {
  touchCount++;
}

// -------------------- SMOOTHING --------------------
float emaMotion = 0.0;
float emaEnv = 0.0;
float emaLight = 0.0;
const float EMA_ALPHA = 0.15;

// Baselines learned during first 30 seconds
float baseMotion = 0.0;
float baseTempC = 24.0;
float baseHum = 45.0;
float baseLight = 2000.0;

// -------------------- HELPERS --------------------
float clamp01(float x) {
  if (x < 0) return 0;
  if (x > 1) return 1;
  return x;
}

float emaUpdate(float prev, float val) {
  return (EMA_ALPHA * val) + ((1.0 - EMA_ALPHA) * prev);
}

float readLightRaw() {
  return (float)analogRead(PIN_LIGHT); // 0..4095
}

bool readTempHum(float &tempC, float &hum) {
#if defined(USE_DHT)
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) return false;
  tempC = t;
  hum = h;
  return true;
#endif

#if defined(USE_SHT31)
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if (isnan(t) || isnan(h)) return false;
  tempC = t;
  hum = h;
  return true;
#endif

  return false;
}

float computeMotionMagnitude() {
  // Uses acceleration magnitude as a proxy for restless movement.
  sensors_event_t accel, gyro, temp;
  lsm6dso.getEvent(&accel, &gyro, &temp);

  float ax = accel.acceleration.x;
  float ay = accel.acceleration.y;
  float az = accel.acceleration.z;

  // magnitude in m/s^2
  float mag = sqrt(ax*ax + ay*ay + az*az);

  // Convert to "movement intensity" by subtracting near-gravity baseline.
  // Typical gravity ~9.8 m/s^2. We measure deviation from that.
  float deviation = fabs(mag - 9.8);

  return deviation; // higher means more movement
}

int motionToScore(float motionDev) {
  // Compare against baseline motion.
  // motionDev is usually small if still, larger if moving.

  float delta = motionDev - baseMotion;

  // Map delta to 0..1 range with a tunable scale.
  // If your sensor is very sensitive, reduce 1.2; if not sensitive, increase it.
  float norm = clamp01(delta / 1.2);

  return (int)(norm * 100.0 + 0.5);
}

int envToScore(float tempC, float hum) {
  // Simple comfort deviation score based on how far temp and humidity drift.
  float tDelta = fabs(tempC - baseTempC);
  float hDelta = fabs(hum - baseHum);

  // Tunable scales: 6C and 20% humidity are treated as "large"
  float tNorm = clamp01(tDelta / 6.0);
  float hNorm = clamp01(hDelta / 20.0);

  // Weighted
  float score = (0.65 * tNorm) + (0.35 * hNorm);
  return (int)(score * 100.0 + 0.5);
}

int lightToPenalty(float lightRaw) {
  // Treat very low light as a stress factor for “work environment”.
  // If your light sensor works inverted, flip comparisons.
  // Here: lower ADC value means darker for many modules, but not all.

  // Baseline is learned, so we only look at deviation from baseline.
  float delta = baseLight - lightRaw; // positive means darker than baseline
  float norm = clamp01(delta / 1200.0); // tunable
  return (int)(norm * 20.0 + 0.5);      // small penalty up to 20 points
}

int touchRatePerMinute() {
  unsigned long now = millis();
  unsigned long elapsed = now - touchWindowStartMs;
  if (elapsed < 1000) return 0;

  // touches per minute = count / (elapsed minutes)
  float minutes = (float)elapsed / 60000.0;
  float rate = touchCount / minutes;

  // reset window every upload interval for stability
  if (elapsed >= UPLOAD_INTERVAL_MS) {
    touchCount = 0;
    touchWindowStartMs = now;
  }

  return (int)(rate + 0.5);
}

int computeStressScore(int motionScore, int envScore, int touchRate, int lightPenalty) {
  // Convert touchRate into a 0..100 score with a tunable cap.
  // Example: 30 touches/min => 100
  float touchNorm = clamp01((float)touchRate / 30.0);
  int touchScore = (int)(touchNorm * 100.0 + 0.5);

  // Weighted sum, plus a small light penalty
  float score = (0.50 * motionScore) + (0.30 * envScore) + (0.20 * touchScore);
  score += lightPenalty;

  if (score < 0) score = 0;
  if (score > 100) score = 100;

  return (int)(score + 0.5);
}

void setIndicators(int stressScore) {
  // Simple LED mapping
  digitalWrite(PIN_LED_G, stressScore < 45 ? HIGH : LOW);
  digitalWrite(PIN_LED_Y, (stressScore >= 45 && stressScore < 70) ? HIGH : LOW);
  digitalWrite(PIN_LED_R, stressScore >= 70 ? HIGH : LOW);

  // Buzz patterns
  if (stressScore >= 80) {
    tone(PIN_BUZZ, 2000, 200);
    delay(250);
    tone(PIN_BUZZ, 2000, 200);
  } else if (stressScore >= 70) {
    tone(PIN_BUZZ, 1500, 150);
  } else {
    noTone(PIN_BUZZ);
  }
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(350);
    Serial.print(".");
    if (millis() - start > 15000) {
      Serial.println("\nWiFi timeout, retrying...");
      WiFi.disconnect(true);
      delay(500);
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      start = millis();
    }
  }
  Serial.println("\nWiFi connected!");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_TOUCH, INPUT);
  pinMode(PIN_BUZZ, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_Y, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);

  digitalWrite(PIN_LED_G, LOW);
  digitalWrite(PIN_LED_Y, LOW);
  digitalWrite(PIN_LED_R, LOW);

  // Touch counting on rising edge
  attachInterrupt(digitalPinToInterrupt(PIN_TOUCH), onTouchRising, RISING);
  touchWindowStartMs = millis();

  // I2C
  Wire.begin(21, 22);

  // LSM6DSO init
  if (!lsm6dso.begin_I2C()) {
    Serial.println("LSM6DSO not found. Check wiring (SDA=21, SCL=22).");
    while (true) delay(100);
  }
  Serial.println("LSM6DSO initialized.");

#if defined(USE_DHT)
  dht.begin();
#endif

#if defined(USE_SHT31)
  if (!sht31.begin(0x44)) {
    Serial.println("SHT31 not found at 0x44. Check wiring.");
    while (true) delay(100);
  }
#endif

  connectWiFi();
  ThingSpeak.begin(client);

  baselineStartMs = millis();
  Serial.println("Baseline calibration started (30 seconds). Keep setup steady.");
}

void loop() {
  // Read sensors
  float motionDev = computeMotionMagnitude();
  float lightRaw = readLightRaw();

  float tempC = NAN, hum = NAN;
  bool thOK = readTempHum(tempC, hum);

  // Update EMAs for stability
  emaMotion = emaUpdate(emaMotion, motionDev);
  emaLight  = emaUpdate(emaLight, lightRaw);

  if (thOK) {
    // no EMA needed, but you can add if you want
  }

  // Baseline learning
  if (!baselineReady) {
    unsigned long elapsed = millis() - baselineStartMs;

    // Learn baseline motion and light
    baseMotion = 0.95 * baseMotion + 0.05 * emaMotion;
    baseLight  = 0.95 * baseLight  + 0.05 * emaLight;

    // Learn baseline temp and humidity only if valid
    if (thOK) {
      baseTempC = 0.95 * baseTempC + 0.05 * tempC;
      baseHum   = 0.95 * baseHum   + 0.05 * hum;
    }

    if (elapsed >= BASELINE_DURATION_MS) {
      baselineReady = true;
      Serial.println("Baseline ready.");
      Serial.printf("BaseMotion=%.3f BaseLight=%.1f BaseTemp=%.1f BaseHum=%.1f\n",
                    baseMotion, baseLight, baseTempC, baseHum);
    }

    delay(50);
    return;
  }

  // Compute scores
  int motionScore = motionToScore(emaMotion);
  int envScore = thOK ? envToScore(tempC, hum) : 0;
  int tRate = touchRatePerMinute();
  int lightPenalty = lightToPenalty(emaLight);

  int stressScore = computeStressScore(motionScore, envScore, tRate, lightPenalty);

  setIndicators(stressScore);

  // Upload
  unsigned long now = millis();
  if (now - lastUploadMs >= UPLOAD_INTERVAL_MS) {
    lastUploadMs = now;

    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    ThingSpeak.setField(1, motionScore);
    ThingSpeak.setField(2, envScore);
    ThingSpeak.setField(3, tRate);
    ThingSpeak.setField(4, stressScore);

    int httpCode = ThingSpeak.writeFields(TS_CHANNEL_ID, TS_WRITE_API_KEY);

    Serial.printf("MotionDev=%.3f Motion=%d Env=%d TouchRate=%d LightRaw=%.1f Stress=%d TS=%d\n",
                  emaMotion, motionScore, envScore, tRate, emaLight, stressScore, httpCode);

    if (thOK) {
      Serial.printf("Temp=%.1fC Hum=%.1f%%\n", tempC, hum);
    } else {
      Serial.println("Temp/Humidity read failed.");
    }
  }

  delay(30);
}
