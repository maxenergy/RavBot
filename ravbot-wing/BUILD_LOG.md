# Ravbot Wing — 30-Day Build Log

Started: **2026-09-01**

This log is the public build diary for the first crowdfunding prototype.

## Day 1 / 30 — Project freeze

### Product decisions

- Existing compact RK3588 8GB/64GB board will be used as the prototype main computer.
- First robot will be built from off-the-shelf electronics and 3D-printed mechanical parts.
- Target size: ~33–36 cm.
- Target architecture: 17 actuators.
- Locomotion policy initially controls only the 10 leg joints.
- Head, beak and wings remain outside the locomotion policy and are controlled by the behavior layer.
- Both wings must be removable.
- Left and right shoulders become standardized **RavPort** expansion interfaces.
- Future RavPort modules may include arms, grippers and wing-shaped hands with fingers.

### 30-day goal

The first prototype is optimized for a crowdfunding demonstration, not production integration.

### Immediate engineering tasks

- [ ] lock prototype leg-servo choice
- [ ] lock head/wing/beak servo choice
- [ ] buy two serial-bus adapters
- [ ] bench-test power distribution
- [ ] determine RK3588 input-power requirement
- [ ] produce first mechanical envelope around the existing RK3588 board
- [ ] design left/right RavPort shoulder datum
- [ ] design one-DoF long wing linkage
- [ ] build initial leg geometry and MJCF model

## Public gates

### Day 7
- complete actuator and power bench
- first printed structural parts

### Day 14
- complete physical robot
- stand for 30 seconds

### Day 21
- autonomous walking ≥1 meter

### Day 27
- 20-minute reliable scripted demonstration

### Day 30
- crowdfunding hero video

---

Updates will be appended here as the physical build progresses.
