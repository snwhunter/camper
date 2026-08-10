# Camper Controller

ESP32-based camper energy controller and touchscreen dashboard for the ESP32-2432S028R (2.8-inch CYD).

## Initial goals

- Display ECO-WORTHY battery/BMS data locally.
- Connect camper telemetry and controls to Home Assistant.
- Adjustable charge limit that drives an output relay/contactor.
- Display sleep/wake behavior:
  - Shore power present: backlight off after 30 seconds of inactivity.
  - No shore power: backlight off after 5 seconds of inactivity.
  - Touch wakes the display.
- Keep controller logic running when the display backlight is off.
- Home Assistant may change settings, but the ESP32 remains authoritative for local charge-limit control.

## Planned data sources

- ECO-WORTHY 12 V 314 Ah Bluetooth BMS over BLE.
- Victron SmartShunt for battery SOC/current.
- Victron MPPT for solar data.
- Shore-power presence input.

## Planned UI

Main battery page:

- SOC
- Battery voltage
- Charge/discharge current
- Battery power
- Battery temperature
- Solar power
- Shore-power state
- Charge-limit setting
- Charge override / charge-to-100% control

Future tabs:

- Battery
- Solar
- Tanks
- Controls

## Hardware

- ESP32-2432S028R / CYD, 240x320 resistive touchscreen
- 12 V to regulated 5 V buck converter
- Isolated or appropriately conditioned shore-power sense input
- Relay-driver output for the charge-enable relay/contactor

## Safety note

The ESP32 GPIO must not directly drive a high-current charger circuit or contactor coil unless the driver circuit is designed for it. Use an appropriate transistor/MOSFET or relay-driver stage, coil suppression, fusing, and isolation where required.
