# CalmSense IoT Behavioral Stress Awareness System

CalmSense is an IoT based behavioral and environmental stress awareness system built using the LILYGO TTGO (ESP32).  
It estimates a stress likelihood score using motion patterns, touch frequency, lighting conditions, and indoor temperature and humidity.  
This project is intended for academic use and stress awareness only. It is not a medical device.

## Features
- Motion monitoring using LSM6DSO accelerometer and gyroscope
- Environmental monitoring using temperature/humidity and light sensors
- Touch frequency tracking using a touch sensor
- Local feedback using LEDs and a buzzer
- Cloud logging using ThingSpeak (real time graphs)

## Hardware Used
- LILYGO TTGO (ESP32)
- LSM6DSO accelerometer and gyroscope (I2C)
- Temperature and humidity sensor (DHT11 or SHT31)
- Light sensor (analog)
- Touch sensor (digital)
- LEDs (3) + resistors
- Buzzer
- Breadboard + jumper wires

## Wiring Summary
- I2C: SDA = GPIO 21, SCL = GPIO 22
- Touch sensor OUT = GPIO 27
- Light sensor OUT = GPIO 34 (ADC)
- Buzzer = GPIO 25
- LEDs: Green = GPIO 13, Yellow = GPIO 12, Red = GPIO 14

More details: `docs/wiring.md`

## Cloud Setup (ThingSpeak)
Create a ThingSpeak channel with 4 fields:
- Field1: MotionScore
- Field2: EnvScore
- Field3: TouchRate (touches per minute)
- Field4: StressScore

Update the firmware with your Channel ID and Write API Key.

## How to Run
1. Open `firmware/CalmSense_Behavioral_Stress_TTGO.ino` in Arduino IDE.
2. Install libraries:
   - ThingSpeak
   - Adafruit LSM6DSO
   - Adafruit Unified Sensor
   - DHT sensor library (if using DHT) OR Adafruit SHT31 (if using SHT31)
3. Update Wi-Fi credentials and ThingSpeak keys.
4. Upload to the LILYGO TTGO.
5. Keep the device steady for 30 seconds during baseline calibration.
6. Move the device, tap the touch sensor, or change lighting and observe the stress score.

## Demo Video
Add your demo link to: `media/demo_video_link.txt`

## Repository Contents
- `firmware/` Arduino code for the TTGO
- `docs/` wiring, architecture, and demo script
- `dashboard/` optional local dashboard
- `media/` block diagram and demo link

## License
MIT License (see `LICENSE`)
