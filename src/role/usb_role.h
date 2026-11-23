#pragma once
#include <Arduino.h>
#include <USB.h>
#include <USBMIDI.h>
#include "tusb.h"

#include "midi_out.h"
#include "usb_device_midi.h"
#include "usb_host_midi.h"

// Escolhe no boot se a placa e HOST USB (toca no FM1) ou DEVICE USB (aparece
// como teclado MIDI), lendo uma ponte de solda entre dois pads vizinhos:
//   sem ponte -> HOST, com ponte -> DEVICE.
// Um mesmo binario tem as duas pilhas, mas so a escolhida e iniciada.
// Requer ARDUINO_USB_MODE=0 e ARDUINO_USB_CDC_ON_BOOT=0 (nada sobe no boot).
class UsbRole : public IMidiOut {
public:
  enum class Mode : uint8_t { Host, Device };
  // Estado do enlace USB, igual nos dois modos para quem monta o LED.
  enum class Link : uint8_t {
    Down,        // nada ligado / nao enumerado
    HostFailed,  // host: a pilha nao iniciou
    NoMidi,      // host: ha aparelho, mas nenhum MIDI
    Ready,       // MIDI pronto
    Suspended,   // device: barramento suspenso
  };

  // Le a ponte e inicia a pilha escolhida. Chamar uma vez no setup().
  void begin(uint8_t pinOut, uint8_t pinIn) {
    mode_ = bridgePresent(pinOut, pinIn) ? Mode::Device : Mode::Host;
    if (mode_ == Mode::Host) {
      hostOk_ = host_.begin();
    } else {
      // Construidos so aqui, para o modo host nao tocar no TinyUSB.
      midi_ = new USBMIDI();
      deviceOut_ = new UsbMidiOut(*midi_);
      midi_->begin();
      USB.productName("Harpa MIDI");
      USB.begin();
    }
  }

  Mode mode() const { return mode_; }
  // false so no modo host, quando a pilha nao iniciou.
  bool started() const { return mode_ == Mode::Device || hostOk_; }

  Link link() const {
    if (mode_ == Mode::Host) {
      return !hostOk_ ? Link::HostFailed
             : host_.connected() ? Link::Ready
             : host_.deviceSeen() ? Link::NoMidi
                                  : Link::Down;
    }
    return tud_suspended() ? Link::Suspended : tud_mounted() ? Link::Ready : Link::Down;
  }

  // IMidiOut: encaminha para a pilha escolhida (descarta antes do begin()).
  void noteOn(uint8_t note, uint8_t velocity, uint8_t channel) override {
    if (mode_ == Mode::Host) host_.noteOn(note, velocity, channel);
    else if (deviceOut_) deviceOut_->noteOn(note, velocity, channel);
  }
  void noteOff(uint8_t note, uint8_t velocity, uint8_t channel) override {
    if (mode_ == Mode::Host) host_.noteOff(note, velocity, channel);
    else if (deviceOut_) deviceOut_->noteOff(note, velocity, channel);
  }

private:
  // pinOut vai a LOW e pinIn, com pull-up, le LOW se houver ponte. Depois os
  // dois voltam a alta impedancia.
  static bool bridgePresent(uint8_t pinOut, uint8_t pinIn) {
    pinMode(pinIn, INPUT_PULLUP);
    pinMode(pinOut, OUTPUT);
    digitalWrite(pinOut, LOW);
    delay(5);
    const bool bridged = digitalRead(pinIn) == LOW;
    pinMode(pinOut, INPUT);
    pinMode(pinIn, INPUT);
    return bridged;
  }

  Mode mode_ = Mode::Host;
  bool hostOk_ = false;
  UsbHostMidiOut host_;
  USBMIDI *midi_ = nullptr;
  UsbMidiOut *deviceOut_ = nullptr;
};
