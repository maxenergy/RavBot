# Ravbot Wing

**An open-source RK3588 Physical AI biped robot with expressive wings, modular hands, local multimodal AI, and reinforcement-learning locomotion.**

> Independent project. Ravbot Wing is not affiliated with, endorsed by, or sponsored by Pollen Robotics or Hugging Face. Microduck is referenced only for technical comparison and ecosystem context.

## Why Ravbot Wing?

Tiny Physical AI robots are having a moment. Projects such as **Microduck** demonstrate how far reinforcement-learning locomotion can go on a small biped platform. Ravbot Wing explores a different direction:

- **RK3588 8GB + 64GB** onboard compute
- local **vision + ASR + TTS + agent** stack
- **10-DoF biped locomotion**
- expressive head, beak, and **dual wings**
- standardized **RavPort** shoulder interface
- wings can later be replaced by **robot arms / dexterous hands**
- 3D-print-first mechanical design
- off-the-shelf components for the first prototype
- open simulation and RL training workflow

## Ravbot Wing vs Microduck

| Feature | Ravbot Wing V1 target | Microduck |
|---|---:|---:|
| Main compute | **RK3588, 8GB/64GB** | RK3566-class platform |
| Physical AI | **Onboard multimodal agent** | RL robot / developer platform |
| Locomotion | RL policy | RL policy |
| Leg DoF | 10 | 10 |
| Expressive appendages | **Replaceable wings / future hands** | No modular wing/hand concept |
| Shoulder expansion | **RavPort** | — |
| Prototype construction | **3D printed + off-the-shelf** | Product hardware |
| Local VLM / ASR / TTS | **Primary design goal** | More constrained |

The goal is **not to clone Microduck**. Ravbot Wing is an independent robot architecture intended to be mechanically, electronically, and visually distinct while learning from the broader open Physical AI ecosystem.

## V1 prototype target

- Height: ~33–36 cm
- Weight: ~1.5–1.8 kg
- 17 actuators total: 10 leg joints, 4 head/neck, 1 beak, 2 wings
- RK3588 8GB + 64GB
- RGB camera, IMU, optional 8×8 ToF
- microphone array + speaker
- Wi-Fi / Bluetooth

## 30-day prototype mission

By Day 30 the robot should stand reliably, walk several meters, turn, recover from at least one fall pose, move its head/beak/wings expressively, see and respond to a person, run a local voice/vision agent on RK3588, and demonstrate continuously for 20–30 minutes.

See [PROTOTYPE_30_DAYS.md](PROTOTYPE_30_DAYS.md).

## RavPort

Each shoulder is an expansion port, not a fixed wing mount. Planned modules include 1-DoF wings, 3-DoF arms, grippers, wing-shaped 3-finger hands, and sensor payloads.

See [RAVPORT.md](RAVPORT.md).

## Roadmap

- M0 — Public project launch
- M1 — 30-day 3D-printed prototype
- M2 — crowdfunding demo
- M3 — open simulation + `ravbot_rl`
- M4 — motion-control board
- M5 — RavPort arm / hand expansion
- M6 — DFM / EVT / DVT
- M7 — first production batch

## Discovery keywords

Microduck alternative · Microduck RK3588 · open-source biped robot · Physical AI robot · MuJoCo sim2real · reinforcement-learning robot · AI companion robot · modular robot hand

## Status

🚧 **Very early prototype phase — September 2026**

**Wings today. Hands tomorrow. Physical AI in the real world.**
