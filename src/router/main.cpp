// Perfil s3zero_router: roteador USB MIDI. Varios aparelhos MIDI num hub
// (FM1, teclado, pads, knobs...): o que um envia, os outros recebem.
// O botao BOOT continua tocando a sequencia de teste para todos.
#include <Arduino.h>

#include "button_fsm.h"
#include "midi_out.h"
#include "midi_router.h"
#include "note_sequence.h"
#include "status_led.h"
#include "usb_midi_host.h"

#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif
// 1..16 = forca o canal de tudo que passa (1 = canal de notas padrao do FM1);
// 0 = mantem o canal original.
#ifndef ROUTER_FORCE_CHANNEL
#define ROUTER_FORCE_CHANNEL 1
#endif

// Fonte local: o SequencePlayer fala IMidiOut, aqui viramos pacotes USB-MIDI.
class LocalMidiOut : public IMidiOut {
public:
  void noteOn(uint8_t note, uint8_t velocity, uint8_t channel) override {
    send(0x90 | ((channel - 1) & 0x0F), note, velocity);
  }
  void noteOff(uint8_t note, uint8_t velocity, uint8_t channel) override {
    send(0x80 | ((channel - 1) & 0x0F), note, velocity);
  }

private:
  static void send(uint8_t status, uint8_t d1, uint8_t d2) {
    const uint8_t p[4] = {static_cast<uint8_t>(status >> 4), status,
                          static_cast<uint8_t>(d1 & 0x7F), static_cast<uint8_t>(d2 & 0x7F)};
    usb_midi_host::sendLocal(p);
  }
};

LocalMidiOut localOut;
ButtonFsm button(20);
SequencePlayer player(localOut, TEST_SEQUENCE, TEST_SEQUENCE_LEN);

// LED RGB onboard (WS2812, GPIO21 no S3-Zero).
// Magenta = host USB nao iniciou. Azul = nenhum aparelho MIDI.
// Amarelo = um aparelho MIDI (nada para rotear ainda). Verde = dois ou mais.
// Branco = nota do botao ativa. Piscada azul clara = nota repassada.
// O WS2812 do S3-Zero usa GRB, mas a variante declara RGB, entao trocamos
// aqui, com RGB real nos argumentos.
// Cores em intensidade relativa (0..100); brilho real em status_led.h.
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  const LedRgb c = status_led::color(r, g, b);
  rgbLedWrite(RGB_BUILTIN, c.g, c.r, c.b);
}

static bool hostOk = false;
static constexpr uint32_t BLINK_MS = 40;

static void updateLed() {
  static uint8_t last = 0xFF;
  static uint32_t seenNotes = 0;
  static uint32_t blinkUntil = 0;

  const uint32_t notes = usb_midi_host::routedNoteCount();
  if (notes != seenNotes) {
    seenNotes = notes;
    blinkUntil = millis() + BLINK_MS;
  }
  const int count = usb_midi_host::deviceCount();
  const uint8_t state = !hostOk                               ? 5
                        : player.active()                     ? 3
                        : (int32_t)(blinkUntil - millis()) > 0 ? 6
                        : count >= 2                          ? 2
                        : count == 1                          ? 4
                                                              : 1;
  if (state == last) return;
  last = state;
  switch (state) {
    case 1: setLed(0, 0, 100); break;
    case 2: setLed(0, 100, 0); break;
    case 3: setLed(100, 100, 100); break;
    case 4: setLed(100, 75, 0); break;
    case 5: setLed(100, 0, 100); break;
    default: setLed(0, 75, 100); break;
  }
}

void setup() {
  setLed(0, 0, 100);
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[router] boot");
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);  // BOOT: nivel baixo = pressionado

  RouterConfig cfg;  // padroes: so cabo 0, so mensagens de canal
  cfg.forceChannel = ROUTER_FORCE_CHANNEL;
  hostOk = usb_midi_host::begin(cfg);
  if (!hostOk) {
    Serial.println("[router] falha ao iniciar o host USB");
  } else if (cfg.forceChannel) {
    Serial.printf("[router] host USB iniciado, canal forcado para %u\n", cfg.forceChannel);
  } else {
    Serial.println("[router] host USB iniciado, canal original mantido");
  }
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
