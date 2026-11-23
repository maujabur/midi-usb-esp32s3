#include "lidar.h"
#include <Arduino.h>

// Margem para comecar a consultar o sensor um pouco antes do fim do periodo
// de medicao, evitando uma leitura I2C de dataReady() a cada volta do loop.
static constexpr uint32_t POLL_EARLY_MS = 3;

void LidarSensor::holdInReset() {
  pinMode(config_.xshutPin, OUTPUT);
  digitalWrite(config_.xshutPin, LOW);
  available_ = false;
}

void LidarSensor::fail() { holdInReset(); }

bool LidarSensor::begin(TwoWire &bus, uint32_t nowMs) {
  driver_ = VL53L1X{};
  driver_.setBus(&bus);
  driver_.setTimeout(SENSOR_TIMEOUT_MS);
  // Libera o XSHUT como entrada: o pull-up do modulo faz a liberacao.
  pinMode(config_.xshutPin, INPUT);
  delay(10);
  if (!driver_.init()) {
    fail();
    return false;
  }
  driver_.setAddress(config_.i2cAddress);
  if (driver_.last_status != 0 || !driver_.setDistanceMode(VL53L1X::Short) ||
      driver_.last_status != 0 ||
      !driver_.setMeasurementTimingBudget(MEASUREMENT_BUDGET_US) || driver_.last_status != 0) {
    fail();
    return false;
  }
  driver_.setROISize(config_.roi.width, config_.roi.height);
  if (driver_.last_status != 0) {
    fail();
    return false;
  }
  driver_.setROICenter(harpa_cfg::spadNumber(config_.roi.centerX, config_.roi.centerY));
  if (driver_.last_status != 0) {
    fail();
    return false;
  }
  driver_.startContinuous(MEASUREMENT_PERIOD_MS);
  if (driver_.last_status != 0) {
    fail();
    return false;
  }
  available_ = true;
  lastSampleMs_ = nowMs;  // referencia para detectar sensor morto
  nextPollMs_ = nowMs;
  return true;
}

bool LidarSensor::service(uint32_t nowMs, Reading &out) {
  if (!available_ || (int32_t)(nowMs - nextPollMs_) < 0 || !driver_.dataReady()) {
    return false;
  }
  out.distanceMm = driver_.read(false);
  out.valid = !driver_.timeoutOccurred() && driver_.ranging_data.range_status == VL53L1X::RangeValid;
  out.updatedAtMs = nowMs;
  lastSampleMs_ = nowMs;
  nextPollMs_ = nowMs + MEASUREMENT_PERIOD_MS - POLL_EARLY_MS;
  return true;
}

uint8_t LidarArray::begin(TwoWire &bus, uint32_t nowMs) {
  bus_ = &bus;
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    readings_[i] = Reading{};
    sensors_[i].configure(SENSOR_CONFIGS[i]);
    sensors_[i].holdInReset();
  }
  delay(2);
  bus.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  bus.setClock(I2C_CLOCK_HZ);
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    sensors_[i].begin(bus, millis());
  }
  lastRetryMs_ = nowMs;
  return availableCount();
}

uint8_t LidarArray::availableCount() const {
  uint8_t n = 0;
  for (size_t i = 0; i < SENSOR_COUNT; ++i) n += sensors_[i].available() ? 1 : 0;
  return n;
}

void LidarArray::service(uint32_t nowMs, bool allowRetry) {
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    LidarSensor &s = sensors_[i];
    if (!s.available()) continue;
    s.service(nowMs, readings_[i]);
    if ((uint32_t)(nowMs - s.lastSampleMs()) > DEAD_AFTER_MS) {
      s.fail();  // parou de responder: volta para a fila de nova tentativa
      readings_[i].valid = false;
    }
  }
  if (allowRetry && (uint32_t)(nowMs - lastRetryMs_) >= RETRY_INTERVAL_MS) {
    lastRetryMs_ = nowMs;
    retryOne(nowMs);
  }
}

// Tenta reiniciar o proximo sensor indisponivel (um por vez, em rodizio).
void LidarArray::retryOne(uint32_t nowMs) {
  for (size_t k = 0; k < SENSOR_COUNT; ++k) {
    const size_t i = (nextRetry_ + k) % SENSOR_COUNT;
    if (sensors_[i].available()) continue;
    nextRetry_ = (i + 1) % SENSOR_COUNT;
    if (sensors_[i].begin(*bus_, millis())) {
      Serial.printf("Lidar %u: RECOVERED at 0x%02X\n", (unsigned)i, SENSOR_CONFIGS[i].i2cAddress);
    }
    return;
  }
}
