# Ravbot Wing V1 — Prototype BOM

Status: **v0.1 / sourcing**

The first prototype intentionally prioritizes speed and availability over production cost.

| Subsystem | Prototype choice | Qty | Status |
|---|---|---:|---|
| Main compute | Existing compact RK3588 8GB/64GB board | 1 | available |
| Leg actuators | serial-bus high-torque servo, final SKU TBD after bench test | 10 | source now |
| Head/neck actuators | compact serial-bus servo | 4 | source now |
| Wing actuators | compact serial-bus servo | 2 | source now |
| Beak actuator | compact serial-bus servo | 1 | source now |
| Servo adapter | USB/UART to half-duplex serial bus | 2 | source now |
| RGB camera | UVC USB camera | 1 | source now |
| IMU | breakout module | 1–2 | source now |
| ToF | 8×8 module | 1 | optional / P1 |
| Microphones | USB microphone array | 1 | source now |
| Speaker | USB audio or amp + speaker | 1 | source now |
| Battery | high-discharge pack matched to servo voltage | 1 | after servo freeze |
| Compute regulator | matched to RK3588 board input | 1 | verify first |
| Servo regulator | separate expression-servo rail if required | 1 | after servo freeze |
| Emergency cutoff | high-current switch / fuse | 1 | required |
| Structure | PETG/PA-CF/MJF printed parts | 1 set | CAD in progress |
| Feet | TPU 95A contact parts | 2 | CAD in progress |

## Prototype sourcing rules

1. No custom PCB is required for the 30-day crowdfunding build unless an off-the-shelf module blocks progress.
2. Compute power and actuator power must be isolated enough that servo transients do not reboot RK3588.
3. High-current actuator power should not flow through small servo-adapter connectors.
4. Leg-servo selection is frozen only after torque, current, speed, backlash and thermal bench tests.
5. The prototype may cost substantially more than the eventual production BOM.

## Post-crowdfunding consolidation

Successful prototype modules will later collapse into:

- Ravbot Motion Board
- power distribution / battery management
- standardized RavPort connectors
- production harness
- factory calibration and test interface
