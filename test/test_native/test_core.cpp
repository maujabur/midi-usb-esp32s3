#include <unity.h>

#include <vector>

#include "button_fsm.h"
#include "note_sequence.h"

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

void test_debounce_ignores_bounce() {
  ButtonFsm b(20);
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, 100));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(false, 105));  // ruido
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, 110));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, 125));
  TEST_ASSERT_EQUAL(ButtonEvent::Pressed, b.update(true, 130));
  TEST_ASSERT_EQUAL(ButtonEvent::None, b.update(true, 200));  // sem repeticao
}

void test_debounce_release() {
  ButtonFsm b(20);
  b.update(true, 0);
  TEST_ASSERT_EQUAL(ButtonEvent::Pressed, b.update(true, 20));
  b.update(false, 100);
  TEST_ASSERT_EQUAL(ButtonEvent::Released, b.update(false, 120));
}

void test_sequence_length() { TEST_ASSERT_EQUAL(36, TEST_SEQUENCE_LEN); }

void test_on_off_paired_and_sequence_order() {
  FakeMidiOut out;
  SequencePlayer p(out, TEST_SEQUENCE, TEST_SEQUENCE_LEN);
  p.onPress();
  p.onRelease();
  p.onPress();
  p.onRelease();
  TEST_ASSERT_EQUAL(4, out.log.size());
  TEST_ASSERT_TRUE(out.log[0].on);
  TEST_ASSERT_EQUAL(63, out.log[0].note);
  TEST_ASSERT_FALSE(out.log[1].on);
  TEST_ASSERT_EQUAL(63, out.log[1].note);
  TEST_ASSERT_EQUAL(62, out.log[2].note);
  TEST_ASSERT_EQUAL(62, out.log[3].note);
}

void test_wraps_after_36() {
  FakeMidiOut out;
  SequencePlayer p(out, TEST_SEQUENCE, TEST_SEQUENCE_LEN);
  for (size_t i = 0; i < TEST_SEQUENCE_LEN; i++) {
    p.onPress();
    p.onRelease();
  }
  TEST_ASSERT_EQUAL(0, p.index());
  p.onPress();
  TEST_ASSERT_EQUAL(63, out.log.back().note);
  TEST_ASSERT_EQUAL(48, TEST_SEQUENCE[TEST_SEQUENCE_LEN - 1]);
}

void test_duplicate_events_ignored() {
  FakeMidiOut out;
  SequencePlayer p(out, TEST_SEQUENCE, TEST_SEQUENCE_LEN);
  p.onRelease();  // sem press: nada
  p.onPress();
  p.onPress();  // duplicado
  TEST_ASSERT_EQUAL(1, out.log.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_debounce_ignores_bounce);
  RUN_TEST(test_debounce_release);
  RUN_TEST(test_sequence_length);
  RUN_TEST(test_on_off_paired_and_sequence_order);
  RUN_TEST(test_wraps_after_36);
  RUN_TEST(test_duplicate_events_ignored);
  return UNITY_END();
}
