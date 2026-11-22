#include <Arduino.h>
#include <USB.h>
#include <USBMIDI.h>
#include "tusb.h"

#include "button_fsm.h"
#include "midi_out.h"
#include "note_sequence.h"

#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif
static const char *USB_MIDI_NAME = "Harpa MIDI";

// Implementacao USB de IMidiOut.
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

USBMIDI midi;
UsbMidiOut midiOut(midi);
ButtonFsm button(20);
SequencePlayer player(midiOut, TEST_SEQUENCE, TEST_SEQUENCE_LEN);

// LED RGB onboard (WS2812, GPIO21 no S3-Zero).
// Azul = energizado, USB ainda nao enumerado pelo host.
// Verde = USB enumerado pelo host (FM1/PC). Branco = nota ativa.
// Amarelo piscando = USB suspenso (host nao fornece barramento ativo).
// O WS2812 do S3-Zero usa ordem GRB, mas a variante declara RGB (verde e
// vermelho saem trocados). Trocamos aqui, com RGB real nos argumentos.
static void setLed(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(RGB_BUILTIN, g, r, b); }

static void updateLed() {
  static uint8_t last = 0xFF;
  uint8_t state;
  if (player.active()) state = 3;
  else if (tud_suspended()) state = (millis() / 500) % 2 ? 4 : 5;
  else if (tud_mounted()) state = 2;
  else state = 1;
  if (state == last) return;
  last = state;
  switch (state) {
    case 1: setLed( 0, 0, 40); break;
    case 2: setLed( 0, 40, 0); break;
    case 3: setLed( 40, 40, 40); break;
    case 4: setLed( 40, 30, 0); break;
    default: setLed( 0, 0, 0); break;
  }
}

void setup() {
  setLed( 0, 0, 40);  // acende ja no boot: placa energizada
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);  // BOOT: nivel baixo = pressionado
  midi.begin();
  USB.productName(USB_MIDI_NAME);
  USB.begin();
}

void loop() {
  updateLed();
  switch (button.update(digitalRead(BOOT_BUTTON_PIN) == LOW, millis())) {
    case ButtonEvent::Pressed:
      player.onPress();
      break;
    case ButtonEvent::Released:
      player.onRelease();
      break;
    default:
      break;
  }
}
