#include <Arduino.h>
#include <Wire.h>
#include "button_fsm.h"
#include "harpa_config.h"
#include "harpa_logic.h"
#include "lidar.h"
#include "note_sequence.h"
#include "status_led.h"
#include "usb_host_midi.h"

// Harpa laser: SENSOR_COUNT VL53L1X no mesmo I2C, cada um como uma corda.
// PRESSED -> Note On, RELEASED -> Note Off, enviados por USB host ao FM1.
// Serial (115200) sai pela UART0, como no perfil s3zero_host.
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

UsbHostMidiOut midiOut;
LidarArray lidars;
HarpaController harpa;
ButtonFsm button(20);
SequencePlayer player(midiOut, TEST_SEQUENCE, TEST_SEQUENCE_LEN, MIDI_CHANNEL, NOTE_VELOCITY);
static bool hostOk = false;

// LED RGB onboard (ver s3zero_host): magenta = host USB nao iniciou, azul =
// nenhum dispositivo, amarelo = dispositivo sem MIDI, verde = FM1 pronto e
// todos os sensores no ar, ciano = FM1 pronto mas faltam sensores,
// branco = alguma corda ou o botao tocando.
static void setLed(uint8_t r, uint8_t g, uint8_t b) {
  const LedRgb c = status_led::color(r, g, b);
  rgbLedWrite(RGB_BUILTIN, c.g, c.r, c.b);
}

static void updateLed(bool anyActive) {
  static uint8_t last = 0xFF;
  const uint8_t state = !hostOk                                  ? 5
                        : anyActive                              ? 3
                        : midiOut.connected()
                            ? (lidars.availableCount() == SENSOR_COUNT ? 2 : 6)
                        : midiOut.deviceSeen()                   ? 4
                                                                 : 1;
  if (state == last) return;
  last = state;
  switch (state) {
    case 1: setLed(0, 0, 100); break;
    case 2: setLed(0, 100, 0); break;
    case 4: setLed(100, 75, 0); break;
    case 5: setLed(100, 0, 100); break;
    case 6: setLed(0, 100, 100); break;
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
  delay(500);
  Serial.println("\n[harpa] boot");
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);  // BOOT: nivel baixo = pressionado
  hostOk = midiOut.begin();
  Serial.println(hostOk ? "[harpa] host USB iniciado, aguardando dispositivos"
                        : "[harpa] falha ao iniciar o host USB");

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
    dispatchMidi(frame, midiOut);
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
