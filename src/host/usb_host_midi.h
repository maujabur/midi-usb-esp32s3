#pragma once
#include <stdint.h>

#include "midi_out.h"

// 1 = logs detalhados (mudancas na porta, registradores a cada 5 s).
#ifndef USB_HOST_DIAG
#define USB_HOST_DIAG 0
#endif

// Saida MIDI via USB Host (ESP-IDF usb_host). O ESP32-S3 faz o papel do
// computador: enumera o dispositivo (ex.: M-Vave FM1), acha a interface
// MIDIStreaming e envia pacotes USB-MIDI pelo endpoint bulk OUT.
//
// noteOn/noteOff podem ser chamados de qualquer task: so enfileiram. O envio
// real acontece na task interna. Sem dispositivo conectado, descartam.
class UsbHostMidiOut : public IMidiOut {
public:
  // Instala o host USB e cria as tasks internas. Chamar uma vez no setup().
  bool begin();

  // true quando o dispositivo MIDI foi enumerado e a interface esta reivindicada.
  bool connected() const;

  // true quando o host USB detectou QUALQUER dispositivo (inclusive um hub ou
  // um aparelho sem interface MIDI). Util para diagnostico.
  bool deviceSeen() const;

  // Imprime no Serial o estado dos registradores USB (porta OTG e PHY).
  // So faz algo se compilado com -DUSB_HOST_DIAG=1.
  void dumpDiag() const;

  void noteOn(uint8_t note, uint8_t velocity, uint8_t channel) override;
  void noteOff(uint8_t note, uint8_t velocity, uint8_t channel) override;

private:
  void send(uint8_t status, uint8_t d1, uint8_t d2);
};
