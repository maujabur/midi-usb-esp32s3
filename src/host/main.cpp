#include <Arduino.h>

#include "button_fsm.h"
#include "midi_out.h"
#include "note_sequence.h"
#include "status_led.h"
#include "usb_host_midi.h"

#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

UsbHostMidiOut midiOut;
ButtonFsm button(20);
SequencePlayer player(midiOut, TEST_SEQUENCE, TEST_SEQUENCE_LEN);

// LED RGB onboard (WS2812, GPIO21 no S3-Zero).
// Magenta = host USB nao iniciou. Azul = nenhum dispositivo no barramento.
// Amarelo = algum dispositivo enumerado (ex.: so o hub), mas nenhum MIDI.
// Verde = FM1 enumerado. Branco = nota ativa.
// O WS2812 do S3-Zero usa GRB, mas a variante declara RGB, entao trocamos
// aqui, com RGB real nos argumentos.
// Cores em intensidade relativa (0..100); brilho real em status_led.h.
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  const LedRgb c = status_led::color(r, g, b);
  rgbLedWrite(RGB_BUILTIN, c.g, c.r, c.b);
}

static bool hostOk = false;

static void updateLed() {
  static uint8_t last = 0xFF;
  uint8_t state = !hostOk                ? 5
                  : player.active()      ? 3
                  : midiOut.connected()  ? 2
                  : midiOut.deviceSeen() ? 4
                                         : 1;
  if (state == last) return;
  last = state;
  switch (state) {
    case 1: setLed(0, 0, 100); break;
    case 2: setLed(0, 100, 0); break;
    case 4: setLed(100, 75, 0); break;
    case 5: setLed(100, 0, 100); break;
    default: setLed(100, 100, 100); break;
  }
}

void setup() {
  setLed(0, 0, 100);
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[host] boot");
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);  // BOOT: nivel baixo = pressionado
  hostOk = midiOut.begin();
  Serial.println(hostOk ? "[host] host USB iniciado, aguardando dispositivos"
                        : "[host] falha ao iniciar o host USB");
}

void loop() {
  updateLed();
#if USB_HOST_DIAG
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat >= 5000) {  // sinal de vida + registradores
    lastBeat = millis();
    Serial.printf("[host] vivo: host=%d dispositivos=%d midi=%d\n", hostOk,
                  midiOut.deviceSeen(), midiOut.connected());
    if (!midiOut.connected()) midiOut.dumpDiag();
  }
#endif
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
