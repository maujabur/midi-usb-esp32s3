#include <unity.h>
#include <string.h>
#include <vector>
#include "harpa_config.h"
#include "harpa_logic.h"

struct Msg {
  bool on;
  uint8_t note, vel, ch;
};
struct FakeMidiOut : IMidiOut {
  std::vector<Msg> log;
  void noteOn(uint8_t n, uint8_t v, uint8_t c) override { log.push_back({true, n, v, c}); }
  void noteOff(uint8_t n, uint8_t v, uint8_t c) override { log.push_back({false, n, v, c}); }
};

void setUp() {}
void tearDown() {}

// Leituras validas e frescas a `mm`, atualizadas em `at`.
static void fill(Reading (&r)[SENSOR_COUNT], uint16_t mm, uint32_t at) {
  for (auto &x : r) x = Reading{mm, at, true};
}

// ---- configuracao -------------------------------------------------------

void test_default_config_is_valid() {
  TEST_ASSERT_TRUE(harpa_cfg::sensorsAreValid(SENSOR_CONFIGS, SENSOR_COUNT));
  TEST_ASSERT_EQUAL(10, SENSOR_COUNT);
}

void test_config_rejects_duplicates_and_bad_values() {
  const SensorConfig ok = {4, 0x30, {4, 4, 8, 7}, 800, 850};
  auto check = [&](SensorConfig a, SensorConfig b) {
    const SensorConfig arr[] = {a, b};
    return harpa_cfg::sensorsAreValid(arr, 2);
  };
  SensorConfig other = ok;
  other.xshutPin = 5;
  other.i2cAddress = 0x31;
  TEST_ASSERT_TRUE(check(ok, other));

  SensorConfig dupPin = other;
  dupPin.xshutPin = ok.xshutPin;
  TEST_ASSERT_FALSE(check(ok, dupPin));

  SensorConfig dupAddr = other;
  dupAddr.i2cAddress = ok.i2cAddress;
  TEST_ASSERT_FALSE(check(ok, dupAddr));

  SensorConfig factory = other;
  factory.i2cAddress = 0x29;
  TEST_ASSERT_FALSE(check(ok, factory));

  SensorConfig badHyst = other;
  badHyst.releaseMm = badHyst.triggerMm;
  TEST_ASSERT_FALSE(check(ok, badHyst));

  SensorConfig badRoi = other;
  badRoi.roi = {4, 4, 1, 7};  // centro perto demais da borda
  TEST_ASSERT_FALSE(check(ok, badRoi));
}

void test_gpio_rules() {
  TEST_ASSERT_TRUE(harpa_cfg::isUsableGpio(1));
  TEST_ASSERT_FALSE(harpa_cfg::isUsableGpio(0));   // BOOT
  TEST_ASSERT_FALSE(harpa_cfg::isUsableGpio(3));   // strapping
  TEST_ASSERT_FALSE(harpa_cfg::isUsableGpio(19));  // USB D-
  TEST_ASSERT_FALSE(harpa_cfg::isUsableGpio(21));  // LED
  TEST_ASSERT_FALSE(harpa_cfg::isUsableGpio(43));  // UART0
  // XSHUT nao pode ser o pino de I2C
  const SensorConfig onSda[] = {{I2C_SDA_PIN, 0x30, {4, 4, 8, 7}, 800, 850}};
  TEST_ASSERT_FALSE(harpa_cfg::sensorsAreValid(onSda, 1));
}

void test_scale_validation() {
  const uint8_t iv[] = {0, 5, 7};
  TEST_ASSERT_TRUE(harpa_cfg::scaleIsValid(48, iv, 3, 3));
  TEST_ASSERT_FALSE(harpa_cfg::scaleIsValid(48, iv, 3, 4));    // faltam intervalos
  TEST_ASSERT_FALSE(harpa_cfg::scaleIsValid(125, iv, 3, 3));   // 125+7 > 127
}

void test_rendezvous2_notes_from_c3() {
  const uint8_t expected[] = {48, 53, 55, 56, 58, 59, 60, 62, 63, 65};
  for (size_t i = 0; i < SENSOR_COUNT; ++i) {
    TEST_ASSERT_EQUAL(expected[i], harpa_cfg::noteForString(i));
  }
}

void test_spad_number() {
  TEST_ASSERT_EQUAL(199, harpa_cfg::spadNumber(8, 7));  // centro padrao
  TEST_ASSERT_EQUAL(128, harpa_cfg::spadNumber(0, 0));
  TEST_ASSERT_EQUAL(127, harpa_cfg::spadNumber(0, 8));
}

// ---- cache ----------------------------------------------------------------

void test_cache_state() {
  Reading r{500, 1000, true};
  TEST_ASSERT_EQUAL(CacheState::Valid, cacheState(r, 1000));
  TEST_ASSERT_EQUAL(CacheState::Valid, cacheState(r, 1000 + STALE_AFTER_MS));
  TEST_ASSERT_EQUAL(CacheState::Stale, cacheState(r, 1000 + STALE_AFTER_MS + 1));
  r.valid = false;
  TEST_ASSERT_EQUAL(CacheState::Invalid, cacheState(r, 1000));
  TEST_ASSERT_EQUAL(CacheState::Invalid, cacheState(Reading{}, 5));
}

// ---- controlador ----------------------------------------------------------

void test_press_and_release_with_hysteresis() {
  HarpaController c;
  Reading r[SENSOR_COUNT];
  fill(r, 2000, 100);
  TEST_ASSERT_EQUAL(0, c.update(r, 100).pressedMask);

  r[3] = Reading{700, 110, true};  // < trigger (800)
  HarpaFrame f = c.update(r, 110);
  TEST_ASSERT_EQUAL_HEX16(1u << 3, f.pressedMask);
  TEST_ASSERT_EQUAL_HEX16(1u << 3, f.activeMask);

  r[3] = Reading{700, 120, true};  // continua: nenhum evento novo
  f = c.update(r, 120);
  TEST_ASSERT_EQUAL(0, f.pressedMask | f.releasedMask);

  r[3] = Reading{830, 130, true};  // entre trigger e release: segura
  f = c.update(r, 130);
  TEST_ASSERT_EQUAL(0, f.pressedMask | f.releasedMask);
  TEST_ASSERT_EQUAL_HEX16(1u << 3, f.activeMask);

  r[3] = Reading{900, 140, true};  // > release
  f = c.update(r, 140);
  TEST_ASSERT_EQUAL_HEX16(1u << 3, f.releasedMask);
  TEST_ASSERT_EQUAL(0, f.activeMask);
}

void test_stale_or_invalid_releases_note() {
  HarpaController c;
  Reading r[SENSOR_COUNT];
  fill(r, 2000, 0);
  r[0] = Reading{500, 0, true};
  TEST_ASSERT_EQUAL_HEX16(1, c.update(r, 0).pressedMask);
  // cache envelhece sem amostra nova
  HarpaFrame f = c.update(r, STALE_AFTER_MS + 1);
  TEST_ASSERT_EQUAL_HEX16(1, f.releasedMask);
  // leitura velha nao dispara
  f = c.update(r, STALE_AFTER_MS + 2);
  TEST_ASSERT_EQUAL(0, f.pressedMask);
  // medida invalida tambem solta
  r[0] = Reading{500, 200, true};
  TEST_ASSERT_EQUAL_HEX16(1, c.update(r, 200).pressedMask);
  r[0] = Reading{500, 210, false};
  TEST_ASSERT_EQUAL_HEX16(1, c.update(r, 210).releasedMask);
}

// ---- MIDI -------------------------------------------------------------------

void test_dispatch_midi() {
  FakeMidiOut out;
  HarpaFrame f;
  f.pressedMask = (1u << 0) | (1u << 9);
  f.releasedMask = 1u << 4;
  dispatchMidi(f, out);
  TEST_ASSERT_EQUAL(3, out.log.size());
  // ordem das cordas: 0 (on), 4 (off), 9 (on)
  TEST_ASSERT_TRUE(out.log[0].on);
  TEST_ASSERT_EQUAL(48, out.log[0].note);
  TEST_ASSERT_EQUAL(NOTE_VELOCITY, out.log[0].vel);
  TEST_ASSERT_EQUAL(MIDI_CHANNEL, out.log[0].ch);
  TEST_ASSERT_FALSE(out.log[1].on);
  TEST_ASSERT_EQUAL(58, out.log[1].note);
  TEST_ASSERT_TRUE(out.log[2].on);
  TEST_ASSERT_EQUAL(65, out.log[2].note);
}

// ---- telemetria -------------------------------------------------------------

void test_telemetry_format() {
  Reading r[SENSOR_COUNT];
  fill(r, 1500, 1000);
  r[0] = Reading{742, 994, true};
  r[1] = Reading{600, 800, true};   // STALE, mantem a distancia
  r[2] = Reading{0, 1000, false};   // INVALID -> X
  char buf[TELEMETRY_BUFFER_SIZE];
  const size_t n = formatTelemetry(buf, sizeof(buf), r, 1u << 0, 1000);
  TEST_ASSERT_EQUAL(strlen(buf), n);
  const char expected[] = "DIST 0:742:6:VALID:ACTIVE 1:600:200:STALE:IDLE 2:X:0:INVALID:IDLE";
  TEST_ASSERT_EQUAL_STRING_LEN(expected, buf, sizeof(expected) - 1);
  // um campo por sensor
  size_t fields = 0;
  for (size_t i = 0; i < n; ++i) fields += buf[i] == ' ';
  TEST_ASSERT_EQUAL(SENSOR_COUNT, fields);
}

void test_telemetry_truncates_safely() {
  Reading r[SENSOR_COUNT];
  fill(r, 1500, 1000);
  char small[20];
  const size_t n = formatTelemetry(small, sizeof(small), r, 0, 1000);
  TEST_ASSERT_TRUE(n < sizeof(small));
  TEST_ASSERT_EQUAL(strlen(small), n);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_default_config_is_valid);
  RUN_TEST(test_config_rejects_duplicates_and_bad_values);
  RUN_TEST(test_gpio_rules);
  RUN_TEST(test_scale_validation);
  RUN_TEST(test_rendezvous2_notes_from_c3);
  RUN_TEST(test_spad_number);
  RUN_TEST(test_cache_state);
  RUN_TEST(test_press_and_release_with_hysteresis);
  RUN_TEST(test_stale_or_invalid_releases_note);
  RUN_TEST(test_dispatch_midi);
  RUN_TEST(test_telemetry_format);
  RUN_TEST(test_telemetry_truncates_safely);
  return UNITY_END();
}
