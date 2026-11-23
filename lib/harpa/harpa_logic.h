#pragma once
#include <stdint.h>
#include <stdio.h>
#include "harpa_config.h"
#include "midi_out.h"

// Logica pura da harpa (sem hardware, testavel no PC): cache de leituras,
// maquina de estados PRESSED/RELEASED, saida MIDI e linha de telemetria.

struct Reading {
  uint16_t distanceMm = 0;
  uint32_t updatedAtMs = 0;
  bool valid = false;  // a ultima medida foi utilizavel (sem timeout, range valido)
};

// VL53L0X (lib Pololu): 65535 = timeout. Sem alvo, o sensor devolve valores
// de fora de alcance (~8190), que sao o estado normal de uma corda solta e
// por isso contam como leitura valida (apenas longe).
inline bool vl53l0xRangeValid(uint16_t mm, bool timedOut) { return !timedOut && mm != 65535; }

enum class CacheState : uint8_t { Valid, Stale, Invalid };

// Unica definicao do estado do cache, usada pelo controlador e pela telemetria.
//   Invalid: a ultima medida nao serve (ou nunca houve uma)
//   Stale  : ha medida valida, mas mais velha que STALE_AFTER_MS
inline CacheState cacheState(const Reading &r, uint32_t nowMs) {
  if (!r.valid) return CacheState::Invalid;
  return (uint32_t)(nowMs - r.updatedAtMs) > STALE_AFTER_MS ? CacheState::Stale
                                                            : CacheState::Valid;
}

inline const char *cacheStateName(CacheState s) {
  return s == CacheState::Valid ? "VALID" : (s == CacheState::Stale ? "STALE" : "INVALID");
}

// Mudancas de estado de um ciclo. Bit i = corda i.
struct HarpaFrame {
  uint16_t activeMask = 0;
  uint16_t pressedMask = 0;
  uint16_t releasedMask = 0;
};

class HarpaController {
public:
  // Corda ativa ao cruzar triggerMm para baixo; solta ao passar de releaseMm,
  // ou quando o cache deixa de ser VALID (sensor sem dado nao segura nota).
  HarpaFrame update(const Reading (&readings)[SENSOR_COUNT], uint32_t nowMs) {
    HarpaFrame frame;
    for (size_t i = 0; i < SENSOR_COUNT; ++i) {
      const uint16_t bit = static_cast<uint16_t>(1u << i);
      const Reading &r = readings[i];
      const bool usable = cacheState(r, nowMs) == CacheState::Valid;
      const bool active = (activeMask_ & bit) != 0;
      if (!active && usable && r.distanceMm < SENSOR_CONFIGS[i].triggerMm) {
        activeMask_ |= bit;
        frame.pressedMask |= bit;
      } else if (active && (!usable || r.distanceMm > SENSOR_CONFIGS[i].releaseMm)) {
        activeMask_ &= static_cast<uint16_t>(~bit);
        frame.releasedMask |= bit;
      }
    }
    frame.activeMask = activeMask_;
    return frame;
  }
  uint16_t activeMask() const { return activeMask_; }

private:
  uint16_t activeMask_ = 0;
};

// PRESSED -> Note On, RELEASED -> Note Off. A velocity e fixa por enquanto;
// a distancia (readings[i].distanceMm) podera modula-la no futuro.
inline void dispatchMidi(const HarpaFrame &frame, IMidiOut &out) {
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    const uint16_t bit = static_cast<uint16_t>(1u << i);
    if (frame.releasedMask & bit) out.noteOff(harpa_cfg::noteForString(i), 0, MIDI_CHANNEL);
    if (frame.pressedMask & bit) {
      out.noteOn(harpa_cfg::noteForString(i), NOTE_VELOCITY, MIDI_CHANNEL);
    }
  }
}

// Tamanho suficiente para a linha de telemetria de SENSOR_COUNT campos.
constexpr size_t TELEMETRY_BUFFER_SIZE = 8 + SENSOR_COUNT * 40;

// "DIST i:distancia_mm_ou_X:idade_ms:VALID|STALE|INVALID:ACTIVE|IDLE ..."
// Devolve o tamanho escrito (sem o terminador). A distancia e X so quando a
// ultima medida e invalida; STALE mantem a ultima distancia.
inline size_t formatTelemetry(char *buf, size_t size, const Reading (&readings)[SENSOR_COUNT],
                              uint16_t activeMask, uint32_t nowMs) {
  size_t n = (size_t)snprintf(buf, size, "DIST");
  for (size_t i = 0; i < SENSOR_COUNT && n < size; ++i) {
    const Reading &r = readings[i];
    char dist[8];
    if (r.valid) {
      snprintf(dist, sizeof(dist), "%u", (unsigned)r.distanceMm);
    } else {
      snprintf(dist, sizeof(dist), "X");
    }
    const int w = snprintf(buf + n, size - n, " %u:%s:%lu:%s:%s", (unsigned)i, dist,
                           (unsigned long)(uint32_t)(nowMs - r.updatedAtMs),
                           cacheStateName(cacheState(r, nowMs)),
                           (activeMask >> i) & 1u ? "ACTIVE" : "IDLE");
    if (w < 0) break;
    n += (size_t)w;
  }
  return n < size ? n : size - 1;
}
