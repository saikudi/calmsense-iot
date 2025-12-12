# Wiring

## I2C Devices (LSM6DSO and possibly SHT31)
- SDA -> GPIO 21
- SCL -> GPIO 22
- VCC -> 3.3V
- GND -> GND

## Touch Sensor (digital)
- OUT -> GPIO 27
- VCC -> 3.3V
- GND -> GND

## Light Sensor (analog)
- OUT -> GPIO 34 (ADC)
- VCC -> 3.3V
- GND -> GND

## LEDs (through resistors)
- Green LED -> GPIO 13
- Yellow LED -> GPIO 12
- Red LED -> GPIO 14
- Each LED should be in series with a resistor (example: 220 ohm).

## Buzzer
- Signal -> GPIO 25
- VCC and GND depend on buzzer module type.
