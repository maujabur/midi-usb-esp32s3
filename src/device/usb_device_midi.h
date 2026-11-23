#pragma once
#include <USBMIDI.h>
#include "midi_out.h"

// Implementacao de IMidiOut para o modo dispositivo USB (TinyUSB / USBMIDI).
class UsbMidiOut : public IMidiOut {
public:
  explicit UsbMidiOut(USBMIDI &m) : midi_(m) {}
  void noteOn(uint8_t note, uint8_t velocity, uint8_t channel) override {
    midi_.noteOn(note, velocity, channel);
  }
  void noteOff(uint8_t note, uint8_t velocity, uint8_t channel) override {
    midi_.noteOff(note, velocity, channel);
  }

private:
  USBMIDI &midi_;
};
