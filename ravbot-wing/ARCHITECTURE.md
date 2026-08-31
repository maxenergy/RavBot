# Ravbot Wing System Architecture

## Design principles

1. Keep locomotion independent from expression.
2. Keep the first prototype buildable from existing modules.
3. Use RK3588 for AI and policy inference.
4. Move hard real-time safety into a dedicated motion MCU after the prototype.
5. Treat wings/hands as interchangeable RavPort modules.

## Prototype architecture

```text
                         RK3588
        +------------------+------------------+
        |                  |                  |
      Vision             Agent            Locomotion
        |                  |               Policy
      Camera          ASR / TTS / VLM        |
        |                  |               50 Hz
        +------------------+------------------+
                           |
                  serial bus adapters
                  /                 \
             leg bus             expression bus
              10 DoF          head + beak + wings
```

## Future production architecture

```text
RK3588
  |
  +-- Agent / vision / speech
  +-- RL policy @50 Hz
  |
  +-- USB / UART / CAN
          |
       Motion MCU
          |
  +-------+----------+----------+
  |                  |          |
Servo buses         IMUs       Power safety
```

## Policy boundary

The locomotion policy should initially control only the **10 leg joints**.

Head, wings and beak are controlled by the behavior / expression layer so RL training is simpler and users can replace wings with different RavPort modules without changing the locomotion policy contract.
