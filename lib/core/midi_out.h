#pragma once
#include <stdint.h>

// Interface minima de saida MIDI. Canais 1..16, como na API USBMIDI.
// A implementacao USB fica em src/; testes usam FakeMidiOut.
class IMidiOut {
public:
  virtual ~IMidiOut() = default;
  virtual void noteOn(uint8_t note, uint8_t velocity, uint8_t channel) = 0;
  virtual void noteOff(uint8_t note, uint8_t velocity, uint8_t channel) = 0;
};
