#pragma once
#include <stdint.h>

// Regras de roteamento USB-MIDI, sem hardware (testavel no PC).
//
// Pacote USB-MIDI (4 bytes): [cabo<<4 | CIN, status, dado1, dado2].
// Regra geral: o que chega de um aparelho vai para todos os OUTROS aparelhos
// conectados. Uma fonte local (ex.: botao) vai para todos.

struct RouterConfig {
  // So repassa o cabo virtual 0 (alguns teclados usam outro cabo para
  // botoes de transporte, por exemplo).
  bool cable0Only = true;
  // So mensagens de canal (nota, CC, program change, pitch bend,
  // aftertouch). Descarta SysEx, clock, active sensing etc.
  bool channelMessagesOnly = true;
  // 1..16 = reescreve o canal de todas as mensagens; 0 = mantem o canal.
  uint8_t forceChannel = 1;
};

namespace midi_router {

constexpr int LOCAL_SOURCE = -1;  // origem que nao e um aparelho USB

// Aplica filtros e transformacoes. Devolve false se o pacote deve ser
// descartado; se true, o pacote pode ter sido modificado (canal).
inline bool filter(const RouterConfig &cfg, uint8_t p[4]) {
  const uint8_t cable = p[0] >> 4;
  const uint8_t cin = p[0] & 0x0F;
  if (cfg.cable0Only && cable != 0) return false;
  if (cin == 0x0) return false;  // reservado / preenchimento
  const bool channelMsg = cin >= 0x8 && cin <= 0xE && (p[1] >> 4) == cin;
  if (cfg.channelMessagesOnly && !channelMsg) return false;
  if (channelMsg && cfg.forceChannel >= 1 && cfg.forceChannel <= 16) {
    p[1] = static_cast<uint8_t>((p[1] & 0xF0) | (cfg.forceChannel - 1));
  }
  return true;
}

// Mascara de destinos: todos os aparelhos ativos que aceitam envio, menos a
// propria origem. bit i = aparelho i.
inline uint32_t destinations(uint32_t outCapableMask, int source) {
  if (source < 0 || source >= 32) return outCapableMask;
  return outCapableMask & ~(1u << source);
}

// true para Note On com velocidade > 0 (util para piscar o LED).
inline bool isNoteOn(const uint8_t p[4]) { return (p[0] & 0x0F) == 0x9 && p[3] > 0; }

}  // namespace midi_router
