# 30-Day Prototype Plan

## Day-30 acceptance criteria

The robot must:
- stand for at least 30 seconds without external support,
- walk 3–5 m on a flat indoor surface,
- turn left and right,
- recover from at least one fall configuration,
- perform synchronized and independent wing gestures,
- articulate head and beak,
- respond to a person by voice,
- use the RGB camera for basic visual interaction,
- demonstrate for 20–30 minutes without a crash or brownout.

## Week 1 — Hardware bench + first mechanical build

- Freeze prototype actuator choice
- Bench-test servos and bus adapters
- Validate battery / regulator architecture
- Build first leg and shoulder brackets
- Print torso and wing shells
- Establish joint IDs and limits

**Gate:** every actuator is individually controllable from RK3588.

## Week 2 — Full body + model

- Assemble complete body
- Zero calibration
- IMU integration
- Measure real mass and link geometry
- Build Ravbot MJCF
- Match simulation and real joint conventions

**Gate:** robot stands with support and simulator pose matches real robot.

## Week 3 — Locomotion

- PPO walking policy
- latency/friction/domain randomization
- first sim2real attempts
- tune feet and center of mass
- add fall detection / recovery

**Gate:** autonomous walk ≥1 m.

## Week 4 — Crowdfunding behavior

- wing/head/beak gestures
- camera + vision
- ASR/TTS
- agent behavior layer
- wiring cleanup
- reliability runs
- final shell finishing

**Gate:** 10 consecutive scripted demos and 20-minute continuous run.

## Scope cuts if behind schedule

Cut in this order:
1. ToF
2. display eyes
3. advanced autonomous navigation
4. extra gestures
5. multi-pose recovery

Do **not** cut stable standing, walking, wings, voice interaction, or reliable power.
