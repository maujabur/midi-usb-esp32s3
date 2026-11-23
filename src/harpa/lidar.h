#pragma once
#include <VL53L1X.h>
#include <Wire.h>
#include "harpa_config.h"
#include "harpa_logic.h"

// Um VL53L1X em modo continuo (free-run). service() nunca espera por medida:
// so le quando o sensor avisa que ha dado novo.
class LidarSensor {
public:
  void configure(const SensorConfig &config) { config_ = config; }
  void holdInReset();
  // Libera o XSHUT, move o sensor para o endereco final e inicia a medicao
  // continua. Bloqueia por ate ~100 ms (init do sensor). false se falhar.
  bool begin(TwoWire &bus, uint32_t nowMs);
  // Atualiza `out` se houver amostra nova. Devolve true se atualizou.
  bool service(uint32_t nowMs, Reading &out);
  bool available() const { return available_; }
  // Volta o sensor ao reset e o marca como indisponivel.
  void fail();
  uint32_t lastSampleMs() const { return lastSampleMs_; }

private:
  SensorConfig config_{};
  VL53L1X driver_{};
  bool available_ = false;
  uint32_t lastSampleMs_ = 0;
  uint32_t nextPollMs_ = 0;
};

class LidarArray {
public:
  // Segura todos em reset e inicia um por vez. Devolve quantos subiram.
  uint8_t begin(TwoWire &bus, uint32_t nowMs);
  // Le todos os sensores com dado novo, detecta sensores mortos e, com
  // allowRetry, tenta reiniciar um ausente a cada RETRY_INTERVAL_MS.
  void service(uint32_t nowMs, bool allowRetry);
  const Reading (&readings() const)[SENSOR_COUNT] { return readings_; }
  uint8_t availableCount() const;
  bool sensorAvailable(size_t index) const {
    return index < SENSOR_COUNT && sensors_[index].available();
  }

private:
  void retryOne(uint32_t nowMs);
  TwoWire *bus_ = nullptr;
  LidarSensor sensors_[SENSOR_COUNT];
  Reading readings_[SENSOR_COUNT];
  size_t nextRetry_ = 0;
  uint32_t lastRetryMs_ = 0;
};
