# Ravbot Wing

**An open-source RK3588 Physical AI biped robot with expressive wings, modular hands, local multimodal AI, and reinforcement-learning locomotion.**

> Independent project. Ravbot Wing is not affiliated with, endorsed by, or sponsored by Pollen Robotics or Hugging Face. Microduck is referenced only for technical comparison and ecosystem context.

## 30-Day Build Challenge

**Day 1 / 30 — September 1, 2026**

We are building the first working Ravbot Wing prototype in 30 days using an existing compact **RK3588 8GB/64GB board**, off-the-shelf components, serial-bus servos, and 3D-printed mechanical parts.

**Crowdfunding prototype acceptance target:**

- [ ] stable stand for 30 seconds
- [ ] walk 3–5 meters indoors
- [ ] turn left and right
- [ ] recover from at least one fall pose
- [ ] expressive head + articulated beak
- [ ] independent dual-wing gestures
- [ ] camera-based visual interaction
- [ ] voice input + spoken response
- [ ] local multimodal agent on RK3588
- [ ] 20–30 minute continuous demo without crash or brownout

Follow the daily build notes in [BUILD_LOG.md](BUILD_LOG.md) and the execution plan in [PROTOTYPE_30_DAYS.md](PROTOTYPE_30_DAYS.md).

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

## Prototype architecture

```text
                    RK3588 8GB/64GB
              vision / voice / agent
                         |
                  RL policy @50Hz
                         |
             +-----------+-----------+
             |                       |
       10-DoF leg bus          expression bus
                              head / beak / wings
```

The first prototype intentionally uses existing servo adapters and modules. A dedicated motion-control MCU board is a post-crowdfunding production milestone.

## RavPort — Wings today, hands tomorrow

Each shoulder is an expansion port, not a fixed wing mount.

```text
Left RavPort / Right RavPort
        |
        +-- 1-DoF expressive wing
        +-- 3-DoF robot arm
        +-- gripper
        +-- wing-shaped 3-finger hand
        +-- sensor payload
        +-- community modules
```

See [RAVPORT.md](RAVPORT.md).

## Crowdfunding demo

The first public demo is intentionally focused on a small number of high-value moments:

1. wake up and look at the user,
2. walk toward the user,
3. answer a spoken question,
4. express emotion using head + beak + wings,
5. recover after a fall,
6. finish with both wings fully extended.

Advanced navigation, charging docks, manipulation, and full SDK work come after the prototype proves market demand.

## Roadmap

- **M0 — Public project launch** ← now
- **M1 — 30-day 3D-printed prototype**
- **M2 — crowdfunding demo**
- **M3 — open simulation + `ravbot_rl`**
- **M4 — motion-control board**
- **M5 — RavPort arm / hand expansion**
- **M6 — DFM / EVT / DVT**
- **M7 — first production batch**

## Discovery keywords

Microduck alternative · Microduck RK3588 · open-source biped robot · Physical AI robot · MuJoCo sim2real · reinforcement-learning robot · AI companion robot · modular robot hand

## Status

🚧 **Prototype phase — September 2026**

Current focus: actuator bench test → first 3D-printed body → real/sim joint alignment → locomotion → crowdfunding demo.

**Wings today. Hands tomorrow. Physical AI in the real world.**
