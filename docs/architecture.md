# System Architecture

## Overview
Sensors connect to the LILYGO TTGO (ESP32). The device computes a stress likelihood score and provides immediate feedback using LEDs and a buzzer. Data is transmitted over Wi-Fi to ThingSpeak for storage and visualization.

## Sensors
- LSM6DSO: motion intensity via accelerometer magnitude deviation
- Temperature/Humidity sensor: indoor comfort deviation
- Light sensor: lighting deviation
- Touch sensor: touch count rate per minute

## Communication
- Wi-Fi from ESP32 to ThingSpeak

## Cloud
- ThingSpeak channel stores MotionScore, EnvScore, TouchRate, StressScore
