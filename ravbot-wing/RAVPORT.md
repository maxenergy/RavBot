# RavPort — Modular Shoulder Interface

RavPort turns the left and right shoulders into interchangeable robot-module interfaces.

## V1 philosophy

The crowdfunding robot ships with simple expressive wings. Future users can replace them with:
- robot arms,
- grippers,
- 3-finger hands,
- wing-shaped manipulation hands,
- sensor modules,
- experimental community modules.

## Mechanical target

Prototype:
- keyed 3D-printed shoulder interface
- 2 fasteners
- positive mechanical stop
- separate load path from decorative shell

Production target:
- 3-point datum
- anti-rotation feature
- blind-mate connector
- serviceable module replacement

## Electrical target

Recommended production interface:
- main actuator power rail
- 5 V logic rail
- GND
- CAN-FD
- UART fallback
- module ID / EEPROM or MCU identification

## Software contract

A module should declare:
- module type
- side: left / right / universal
- number of joints
- joint limits
- telemetry capabilities
- power class
- firmware version

Example module IDs:

```text
RAV-WING-1DOF
RAV-ARM-3DOF
RAV-GRIPPER-2F
RAV-WINGHAND-3F
RAV-SENSOR-DEV
```

## Safety

V1 prototype modules are replaced only while powered off. True hot-swap is not a first-generation requirement.
