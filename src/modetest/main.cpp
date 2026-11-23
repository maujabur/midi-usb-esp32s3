#include <Arduino.h>
// O LDF so ve bibliotecas incluidas direto pelos .cpp (usb_role.h as usa)
#include <USB.h>
#include <USBMIDI.h>

#include "button_fsm.h"
#include "note_sequence.h"
#include "status_led.h"
#include "usb_role.h"

// Teste de escolha host/device no boot, num unico binario (usb_role.h).
//
// No boot le a ponte de solda entre dois pinos vizinhos:
//   sem ponte -> HOST USB   (S3 toca no FM1, como o s3zero_host)
//   com ponte -> DEVICE USB (S3 aparece como teclado MIDI, como o s3zero_device)
// O botao BOOT toca a sequencia de teste nos dois modos. Nada mais.
//
// Pinos da ponte (flags do perfil): MODE_PIN_OUT e MODE_PIN_IN.
#ifndef MODE_PIN_OUT
#define MODE_PIN_OUT 5
#endif
#ifndef MODE_PIN_IN
#define MODE_PIN_IN 6
#endif
#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

static UsbRole usb;
static ButtonFsm button(20);
static SequencePlayer player(usb, TEST_SEQUENCE, TEST_SEQUENCE_LEN);

// LED (WS2812 no GPIO21, ordem GRB; ver s3zero_host).
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  const LedRgb c = status_led::color(r, g, b);
  rgbLedWrite(RGB_BUILTIN, c.g, c.r, c.b);
}

// Azul = sem enlace, amarelo = aparelho sem MIDI (host), verde = pronto,
// magenta = host nao iniciou, amarelo/apagado alternando = suspenso (device),
// branco = nota tocando.
static void updateLed() {
  static uint8_t last = 0xFF;
  uint8_t state;
  if (player.active()) {
    state = 3;
  } else {
    switch (usb.link()) {
      case UsbRole::Link::HostFailed: state = 5; break;
      case UsbRole::Link::NoMidi: state = 4; break;
      case UsbRole::Link::Ready: state = 2; break;
      case UsbRole::Link::Suspended: state = (millis() / 500) % 2 ? 4 : 6; break;
      default: state = 1; break;
    }
  }
  if (state == last) return;
  last = state;
  switch (state) {
    case 1: setLed(0, 0, 100); break;
    case 2: setLed(0, 100, 0); break;
    case 4: setLed(100, 75, 0); break;
    case 5: setLed(100, 0, 100); break;
    case 6: setLed(0, 0, 0); break;
    default: setLed(100, 100, 100); break;
  }
}

void setup() {
  setLed(0, 0, 100);
  Serial.begin(115200);  // UART0 (GPIO43/44): o USB nativo e do host/device
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  usb.begin(MODE_PIN_OUT, MODE_PIN_IN);
  const bool device = usb.mode() == UsbRole::Mode::Device;
  // Mostra o modo escolhido: ciano = device, laranja = host.
  if (device) setLed(0, 100, 100);
  else setLed(100, 35, 0);
  delay(700);
  setLed(0, 0, 100);
  Serial.printf("\n[modetest] ponte %s -> modo %s%s\n", device ? "SIM" : "NAO",
                device ? "DEVICE" : "HOST", usb.started() ? "" : " (falha ao iniciar)");
}

void loop() {
  updateLed();
  switch (button.update(digitalRead(BOOT_BUTTON_PIN) == LOW, millis())) {
    case ButtonEvent::Pressed: player.onPress(); break;
    case ButtonEvent::Released: player.onRelease(); break;
    default: break;
  }
}
