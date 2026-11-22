#pragma once
#include <stddef.h>
#include <stdint.h>

#include "midi_out.h"

// Sequencia de teste (C4 = 60), definida em compile time.
// Compassos 184-207: 12 grupos de 3 notas.
constexpr uint8_t TEST_SEQUENCE[] = {
    63, 62, 60,  // 184-185
    59, 58, 56,  // 186-187
    63, 62, 60,  // 188-189
    59, 58, 56,  // 190-191
    63, 62, 60,  // 192-193
    59, 58, 56,  // 194-195
    63, 62, 60,  // 196-197
    59, 58, 56,  // 198-199
    56, 58, 60,  // 200-201
    55, 56, 58,  // 202-203
    53, 55, 56,  // 204-205
    55, 53, 48,  // 206-207
};
constexpr size_t TEST_SEQUENCE_LEN = sizeof(TEST_SEQUENCE) / sizeof(TEST_SEQUENCE[0]);

// Liga o botao a sequencia: Note On ao pressionar, Note Off ao soltar,
// avancando o indice (com loop) a cada ciclo. O Note Off usa sempre a nota
// que foi enviada no Note On correspondente.
class SequencePlayer {
public:
  SequencePlayer(IMidiOut &out, const uint8_t *notes, size_t len, uint8_t channel = 1,
                 uint8_t velocity = 100)
      : out_(out), notes_(notes), len_(len), channel_(channel), velocity_(velocity) {}

  void onPress() {
    if (active_) return;  // ignora Pressed duplicado
    activeNote_ = notes_[index_];
    active_ = true;
    out_.noteOn(activeNote_, velocity_, channel_);
  }

  void onRelease() {
    if (!active_) return;
    out_.noteOff(activeNote_, 0, channel_);
    active_ = false;
    index_ = (index_ + 1) % len_;
  }

  size_t index() const { return index_; }
  bool active() const { return active_; }

private:
  IMidiOut &out_;
  const uint8_t *notes_;
  size_t len_;
  uint8_t channel_;
  uint8_t velocity_;
  size_t index_ = 0;
  uint8_t activeNote_ = 0;
  bool active_ = false;
};
