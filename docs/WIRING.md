# Anemometer Wiring (RPR220 + ESP32 Lolin Lite)

## Assumptions
- Sensor: `RPR220` reflective optocoupler used as a pulse source.
- Rotor pattern: `12 pulses/revolution` (12PPR).
- MCU pin used for pulse input: `GPIO18` (change in firmware if needed).
- ESP32 logic level: `3.3V`.

## Diagram

```text
ESP32 LOLIN LITE                             RPR220 MODULE / DISCRETE WIRING
----------------                             ---------------------------------
3V3  ---------------------------------------> IR LED anode (through 150R to 220R)
GND  ---------------------------------------> IR LED cathode

3V3  ----[10k pull-up]----+-----------------> Phototransistor collector (OUT node)
                          |
GPIO18 -------------------+
                          |
GND  -----------------------> Phototransistor emitter
```

## Practical Notes
- If your RPR220 board already includes resistor network and output conditioning, use:
  - `VCC -> 3V3`
  - `GND -> GND`
  - `OUT -> GPIO18`
- Use a short wire and, if you see bounce/noise, add:
  - `100nF` capacitor from `OUT` to `GND`.
- If pulse polarity is inverted, switch ISR trigger in firmware (`FALLING` <-> `RISING`).
- The conversion from rotational frequency to m/s requires calibration. Firmware exposes `kMpsPerHz` for this.

## Optional Power/Protection
- Add `1k` series resistor between `OUT` and GPIO if wiring is long/noisy.
- Keep all sensor wiring referenced to ESP32 GND.
