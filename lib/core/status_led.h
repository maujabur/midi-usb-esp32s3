#pragma once
#include <stdint.h>

// Brilho do LED RGB de status. Ajuste no platformio.ini ([esp32s3_zero].led_flags).
// LED_LEVEL: brilho maximo de cada canal (0..255).
#ifndef LED_LEVEL
#define LED_LEVEL 20
#endif
// LED_GREEN_PCT: o verde do WS2812 parece bem mais forte ao olho que o
// vermelho e o azul com o mesmo valor; este fator (0..100) o compensa.
#ifndef LED_GREEN_PCT
#define LED_GREEN_PCT 40
#endif

struct LedRgb {
  uint8_t r, g, b;
};

namespace status_led {

// Converte uma intensidade relativa (0..100) no valor do canal. Qualquer
// intensidade > 0 da pelo menos 1, para a cor nao sumir com brilho baixo.
inline uint8_t scale(uint8_t pct, uint32_t factorPct) {
  if (pct == 0) return 0;
  const uint32_t v = (uint32_t)pct * LED_LEVEL * factorPct / 10000u;
  return v == 0 ? 1 : (v > 255 ? 255 : (uint8_t)v);
}

// Cor em intensidade relativa por canal (0..100) -> valores para o LED.
inline LedRgb color(uint8_t r, uint8_t g, uint8_t b) {
  return {scale(r, 100), scale(g, LED_GREEN_PCT), scale(b, 100)};
}

}  // namespace status_led
