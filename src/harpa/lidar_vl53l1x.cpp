#if !defined(HARPA_SENSOR_VL53L0X)
#include "lidar.h"
#include <Arduino.h>

// Driver VL53L1X (lib Pololu): ROI configuravel, modo de distancia curto.
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
#endif  // !HARPA_SENSOR_VL53L0X
