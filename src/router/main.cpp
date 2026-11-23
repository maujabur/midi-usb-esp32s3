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
// Branco = nota do botao ativa. Vermelho = roteando: aceso enquanto chegam
// mensagens de um aparelho para outro(s), e ate ROUTING_HOLD_MS depois.
// O WS2812 do S3-Zero usa GRB, mas a variante declara RGB, entao trocamos
// aqui, com RGB real nos argumentos.
// Cores em intensidade relativa (0..100); brilho real em status_led.h.
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  const LedRgb c = status_led::color(r, g, b);
  rgbLedWrite(RGB_BUILTIN, c.g, c.r, c.b);
}

static bool hostOk = false;
// Tempo que o vermelho fica aceso depois da ultima mensagem roteada. Longo o
// bastante para ver uma nota isolada; com knobs/acordes fica aceso direto.
static constexpr uint32_t ROUTING_HOLD_MS = 150;

enum class LedState : uint8_t { None, NoDevice, OneDevice, Ready, Routing, Button, HostFail };

static void updateLed() {
  static LedState last = LedState::None;
  static uint32_t seenRouted = 0;
  static uint32_t routingUntil = 0;

  const uint32_t routed = usb_midi_host::routedPacketCount();
  if (routed != seenRouted) {
    seenRouted = routed;
    routingUntil = millis() + ROUTING_HOLD_MS;
  }
  const bool routing = (int32_t)(routingUntil - millis()) > 0;
  const int count = usb_midi_host::deviceCount();
  const LedState state = !hostOk           ? LedState::HostFail
                         : player.active() ? LedState::Button
                         : routing         ? LedState::Routing
                         : count >= 2      ? LedState::Ready
                         : count == 1      ? LedState::OneDevice
                                           : LedState::NoDevice;
  if (state == last) return;
  last = state;
  switch (state) {
    case LedState::NoDevice: setLed(0, 0, 100); break;     // azul
    case LedState::OneDevice: setLed(100, 75, 0); break;   // amarelo
    case LedState::Ready: setLed(0, 100, 0); break;        // verde
    case LedState::Routing: setLed(100, 0, 0); break;      // vermelho
    case LedState::Button: setLed(100, 100, 100); break;   // branco
    case LedState::HostFail: setLed(100, 0, 100); break;   // magenta
    default: break;
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
