#if defined(HARPA_SENSOR_VL53L0X)
#include "lidar.h"
#include <Arduino.h>

// Driver VL53L0X (lib Pololu). Sem ROI nem modo de distancia. A lib so tem a
// leitura continua que ESPERA o dado, entao antes de ler consultamos o mesmo
// registrador de status que ela usa; com o dado pronto, a leitura volta na
// hora e o loop nunca espera.

bool LidarSensor::begin(TwoWire &bus, uint32_t nowMs) {
  driver_ = RangeDriver{};
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
  if (driver_.last_status != 0 || !driver_.setMeasurementTimingBudget(MEASUREMENT_BUDGET_US)) {
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
  if (!available_ || (int32_t)(nowMs - nextPollMs_) < 0 ||
      (driver_.readReg(RangeDriver::RESULT_INTERRUPT_STATUS) & 0x07) == 0) {
    return false;
  }
  const uint16_t mm = driver_.readRangeContinuousMillimeters();
  out.distanceMm = mm;
  out.valid = vl53l0xRangeValid(mm, driver_.timeoutOccurred());
  out.updatedAtMs = nowMs;
  lastSampleMs_ = nowMs;
  nextPollMs_ = nowMs + MEASUREMENT_PERIOD_MS - POLL_EARLY_MS;
  return true;
}
#endif  // HARPA_SENSOR_VL53L0X
