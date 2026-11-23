#include <Arduino.h>
// O LDF so ve bibliotecas incluidas direto pelos .cpp (usb_role.h as usa)
#include <USB.h>
#include <USBMIDI.h>
#include <Wire.h>
#include "button_fsm.h"
#include "harpa_config.h"
#include "harpa_logic.h"
#include "lidar.h"
#include "note_sequence.h"
#include "status_led.h"
#include "usb_role.h"

// Harpa laser: SENSOR_COUNT sensores de distancia no mesmo I2C, cada um uma
// corda. PRESSED -> Note On, RELEASED -> Note Off.
// A saida USB e escolhida no boot por uma ponte de solda nos pads
// MODE_PIN_OUT/MODE_PIN_IN (GPIO42/45): sem ponte = HOST (toca no FM1), com
// ponte = DEVICE (aparece como teclado MIDI "Harpa MIDI").
// Serial (115200) sai pela UART0.
// O botao BOOT toca a sequencia de teste (como nos outros perfis), mesmo que
// nenhum sensor tenha iniciado.
//
// 1 = linha DIST com as distancias e o estado do cache (calibracao).
#ifndef HARPA_TELEMETRY
#define HARPA_TELEMETRY 0
#endif
constexpr uint32_t TELEMETRY_PERIOD_MS = 100;
#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 0
#endif

UsbRole usb;
LidarArray lidars;
HarpaController harpa;
ButtonFsm button(20);
SequencePlayer player(usb, TEST_SEQUENCE, TEST_SEQUENCE_LEN, MIDI_CHANNEL, NOTE_VELOCITY);

// LED RGB onboard (ver s3zero_host): magenta = host USB nao iniciou, azul =
// sem enlace USB, amarelo = aparelho sem MIDI (host) ou barramento suspenso
// (device, piscando), verde = MIDI pronto e todos os sensores no ar, ciano =
// MIDI pronto mas faltam sensores, branco = alguma corda ou o botao tocando.
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  const LedRgb c = status_led::color(r, g, b);
  rgbLedWrite(RGB_BUILTIN, c.g, c.r, c.b);
}

static void updateLed(bool anyActive) {
  static uint8_t last = 0xFF;
  uint8_t state;
  if (anyActive) {
    state = 3;
  } else {
    switch (usb.link()) {
      case UsbRole::Link::HostFailed: state = 5; break;
      case UsbRole::Link::NoMidi: state = 4; break;
      case UsbRole::Link::Ready: state = lidars.availableCount() == SENSOR_COUNT ? 2 : 6; break;
      case UsbRole::Link::Suspended: state = (millis() / 500) % 2 ? 4 : 7; break;
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
    case 6: setLed(0, 100, 100); break;
    case 7: setLed(0, 0, 0); break;
    default: setLed(100, 100, 100); break;
  }
}

static void printTransitions(const HarpaFrame &frame) {
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    const uint16_t bit = static_cast<uint16_t>(1u << i);
    if (frame.pressedMask & bit) Serial.printf("String %u PRESSED\n", (unsigned)i);
    if (frame.releasedMask & bit) Serial.printf("String %u RELEASED\n", (unsigned)i);
  }
}

#if HARPA_TELEMETRY
static void printTelemetry(const HarpaFrame &frame, uint32_t nowMs) {
  static uint32_t lastMs = 0;
  static bool started = false;
  if (started && (uint32_t)(nowMs - lastMs) < TELEMETRY_PERIOD_MS) return;
  started = true;
  lastMs = nowMs;
  char line[TELEMETRY_BUFFER_SIZE];
  formatTelemetry(line, sizeof(line), lidars.readings(), frame.activeMask, nowMs);
  Serial.println(line);
}
#endif

void setup() {
  setLed(0, 0, 100);
  Serial.begin(115200);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);  // BOOT: nivel baixo = pressionado

  usb.begin(MODE_PIN_OUT, MODE_PIN_IN);
  const bool device = usb.mode() == UsbRole::Mode::Device;
  // Mostra o modo escolhido: ciano = device, laranja = host.
  if (device) setLed(0, 100, 100);
  else setLed(100, 35, 0);
  delay(700);
  setLed(0, 0, 100);
  Serial.printf("\n[harpa] boot, ponte %s -> modo %s%s\n", device ? "SIM" : "NAO",
                device ? "DEVICE" : "HOST", usb.started() ? "" : " (falha ao iniciar o USB)");

  const uint8_t available = lidars.begin(Wire, millis());
  Serial.printf("Lidars available: %u/%u\n", (unsigned)available, (unsigned)SENSOR_COUNT);
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    Serial.printf("Lidar %u: %s at 0x%02X (nota %u)\n", (unsigned)i,
                  lidars.sensorAvailable(i) ? "OK" : "FAILED", SENSOR_CONFIGS[i].i2cAddress,
                  (unsigned)harpa_cfg::noteForString(i));
  }
}

void loop() {
  const uint32_t nowMs = millis();
  // Nova tentativa de sensores ausentes so com todas as cordas soltas, pois
  // ela pode bloquear o loop por ~100 ms.
  lidars.service(nowMs, harpa.activeMask() == 0 && !player.active());
  const HarpaFrame frame = harpa.update(lidars.readings(), nowMs);
  if (frame.pressedMask | frame.releasedMask) {
    dispatchMidi(frame, usb);
    printTransitions(frame);
  }
#if HARPA_TELEMETRY
  printTelemetry(frame, nowMs);
#endif
  switch (button.update(digitalRead(BOOT_BUTTON_PIN) == LOW, nowMs)) {
    case ButtonEvent::Pressed: player.onPress(); break;
    case ButtonEvent::Released: player.onRelease(); break;
    default: break;
  }
  updateLed(frame.activeMask != 0 || player.active());
}
