# Smart Poultry House Monitor 🐔

ESP32-based system that monitors ammonia, CO₂, temperature and humidity in a poultry house, controls ventilation and evaporative cooling automatically, and sends alerts via Telegram and SMS.

## Problem
Heat stress and ammonia buildup are major causes of mortality in poultry houses. This project provides low-cost, real-time monitoring and automatic climate response.

## Features
- Real-time NH₃, CO₂, temperature and humidity monitoring
- Heat stress index (THI) calculation
- Automatic fan and cooling-pad control with hysteresis
- Age-based target temperature for broilers
- Manual/auto switch and fail-safe behavior on sensor failure
- Mains power-loss detection with battery backup
- Data logging to Google Sheets
- Telegram alerts + SMS fallback via SIM module
- Telegram commands: /status, /fan_on, /auto, /age N
- Watchdog for automatic recovery

## Hardware
| Component | Purpose |
|---|---|
| ESP32 | Main controller |
| MQ137 | Ammonia |
| MH-Z19B | CO₂ (NDIR) |
| SHT31 | Temperature & humidity |
| SIM800L | SMS alerts |
| Relay module + contactors | Fans, cooling pump, siren |

## Wiring

| Module | Pin | ESP32 |
|---|---|---|
| SHT31 | SDA / SCL / VCC | GPIO21 / GPIO22 / 3.3V |
| MH-Z19B | TX / RX / Vin | GPIO16 / GPIO17 / 5V |
| MQ137 | AO (via 10k/20k divider) | GPIO34 |
| MQ137 | VCC | 5V |
| SIM800L | TX / RX | GPIO26 / GPIO27 |
| Relay IN1 | Exhaust fans | GPIO23 |
| Relay IN2 | Cooling pad pump | GPIO19 |
| Relay IN3 | Siren | GPIO18 |
| Manual switch | Between pin and GND | GPIO32 |
| Mains sense | Adapter 5V via divider | GPIO33 |

**Power notes**
- SIM800L needs its own 4V / 2A supply (common GND with ESP32)
- Fans and pump are switched through contactors, never directly by the relay
- Add a manual bypass switch on the fan contactor
- Mains (220V) wiring must be done by a qualified electrician



## Setup
1. Install Arduino libraries: Adafruit SHT31, Adafruit BusIO
2. Copy `secrets.example.h` to `secrets.h` and fill in Wi-Fi, Telegram and Sheets details
3. Deploy the Google Apps Script (`sheets/Code.gs`) as a web app
4. Upload the firmware

## Calibration
The MQ137 needs a 24h burn-in and calibration in clean air (R0). Thresholds are starting values and should be adjusted for your flock and breed.

## Project status
Firmware and design complete. Hardware testing in progress.

## Safety
Mains-voltage wiring (220V) must be done by a qualified electrician. Always add a manual bypass for ventilation.

## Future work
Water flow monitoring, dust sensor, multiple sensing nodes, dashboard.
