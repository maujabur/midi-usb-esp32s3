#include "lidar.h"
#include <Arduino.h>

void LidarSensor::holdInReset() {
  pinMode(config_.xshutPin, OUTPUT);
  digitalWrite(config_.xshutPin, LOW);
  available_ = false;
}

void LidarSensor::fail() { holdInReset(); }

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
