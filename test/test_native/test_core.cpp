#include <unity.h>

#include <vector>

#include "button_fsm.h"
#include "midi_router.h"
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

// --- roteador ---

void test_router_forces_channel() {
  RouterConfig cfg;  // forceChannel = 1
  uint8_t p[4] = {0x09, 0x99, 36, 100};  // Note On canal 10 (pads)
  TEST_ASSERT_TRUE(midi_router::filter(cfg, p));
  TEST_ASSERT_EQUAL_HEX8(0x90, p[1]);
  TEST_ASSERT_EQUAL(36, p[2]);
  TEST_ASSERT_EQUAL(100, p[3]);
}

void test_router_keeps_channel_when_disabled() {
  RouterConfig cfg;
  cfg.forceChannel = 0;
  uint8_t p[4] = {0x0B, 0xB4, 7, 64};  // CC canal 5
  TEST_ASSERT_TRUE(midi_router::filter(cfg, p));
  TEST_ASSERT_EQUAL_HEX8(0xB4, p[1]);
}

void test_router_drops_sysex_clock_and_other_cables() {
  RouterConfig cfg;
  uint8_t sysex[4] = {0x04, 0xF0, 0x43, 0x10};
  uint8_t clock[4] = {0x0F, 0xF8, 0, 0};
  uint8_t sensing[4] = {0x0F, 0xFE, 0, 0};
  uint8_t cable1[4] = {0x19, 0x90, 60, 100};
  uint8_t padding[4] = {0, 0, 0, 0};
  TEST_ASSERT_FALSE(midi_router::filter(cfg, sysex));
  TEST_ASSERT_FALSE(midi_router::filter(cfg, clock));
  TEST_ASSERT_FALSE(midi_router::filter(cfg, sensing));
  TEST_ASSERT_FALSE(midi_router::filter(cfg, cable1));
  TEST_ASSERT_FALSE(midi_router::filter(cfg, padding));
}

void test_router_passes_sysex_when_allowed() {
  RouterConfig cfg;
  cfg.channelMessagesOnly = false;
  uint8_t sysex[4] = {0x04, 0xF0, 0x43, 0x10};
  TEST_ASSERT_TRUE(midi_router::filter(cfg, sysex));
  TEST_ASSERT_EQUAL_HEX8(0xF0, sysex[1]);  // canal so mexe em mensagem de canal
}

void test_router_destinations_exclude_source() {
  const uint32_t outs = 0b1011;  // aparelhos 0, 1 e 3 recebem
  TEST_ASSERT_EQUAL_HEX32(0b1010, midi_router::destinations(outs, 0));
  TEST_ASSERT_EQUAL_HEX32(0b1011, midi_router::destinations(outs, 2));  // 2 so envia
  TEST_ASSERT_EQUAL_HEX32(0b1011, midi_router::destinations(outs, midi_router::LOCAL_SOURCE));
}

void test_router_note_on_detection() {
  const uint8_t on[4] = {0x09, 0x90, 60, 100};
  const uint8_t onZero[4] = {0x09, 0x90, 60, 0};  // Note On vel 0 = Note Off
  const uint8_t off[4] = {0x08, 0x80, 60, 0};
  TEST_ASSERT_TRUE(midi_router::isNoteOn(on));
  TEST_ASSERT_FALSE(midi_router::isNoteOn(onZero));
  TEST_ASSERT_FALSE(midi_router::isNoteOn(off));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_router_forces_channel);
  RUN_TEST(test_router_keeps_channel_when_disabled);
  RUN_TEST(test_router_drops_sysex_clock_and_other_cables);
  RUN_TEST(test_router_passes_sysex_when_allowed);
  RUN_TEST(test_router_destinations_exclude_source);
  RUN_TEST(test_router_note_on_detection);
  RUN_TEST(test_debounce_ignores_bounce);
  RUN_TEST(test_debounce_release);
  RUN_TEST(test_sequence_length);
  RUN_TEST(test_on_off_paired_and_sequence_order);
  RUN_TEST(test_wraps_after_36);
  RUN_TEST(test_duplicate_events_ignored);
  return UNITY_END();
}
