#pragma once
#include <stdint.h>

// Debounce de botao. Codigo puro: recebe leitura bruta e tempo, devolve evento.
enum class ButtonEvent : uint8_t { None, Pressed, Released };

class ButtonFsm {
public:
  explicit ButtonFsm(uint32_t debounceMs = 20) : debounceMs_(debounceMs) {}

  // rawPressed: true quando o botao esta pressionado (ja considerando polaridade).
  ButtonEvent update(bool rawPressed, uint32_t nowMs) {
    if (rawPressed != lastRaw_) {
      lastRaw_ = rawPressed;
      lastChangeMs_ = nowMs;
    }
    if (rawPressed != stable_ && (uint32_t)(nowMs - lastChangeMs_) >= debounceMs_) {
      stable_ = rawPressed;
      return stable_ ? ButtonEvent::Pressed : ButtonEvent::Released;
    }
    return ButtonEvent::None;
  }

  bool pressed() const { return stable_; }

private:
  uint32_t debounceMs_;
  bool lastRaw_ = false;
  bool stable_ = false;
  uint32_t lastChangeMs_ = 0;
};
