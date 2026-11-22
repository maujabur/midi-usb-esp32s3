#pragma once
#include <stdint.h>

#include "midi_router.h"

// 1 = logs detalhados (cada pacote roteado, eventos de dispositivos).
#ifndef USB_HOST_DIAG
#define USB_HOST_DIAG 0
#endif

// Host USB MIDI para varios aparelhos (direto ou atras de hub), com
// roteamento: o que chega de um aparelho vai para todos os outros
// (regras em lib/core/midi_router.h).
//
// Toda a contabilidade dos aparelhos roda numa unica task (a do cliente USB);
// as outras tasks so enfileiram pacotes locais e leem contadores.
namespace usb_midi_host {

constexpr int MAX_DEVICES = 4;

// Instala o host USB e cria as tasks. Chamar uma vez no setup().
bool begin(const RouterConfig &cfg);

// Envia um pacote USB-MIDI de origem local (ex.: botao) para todos os
// aparelhos. Seguro de chamar de qualquer task; passa pelos mesmos filtros.
void sendLocal(const uint8_t packet[4]);

// Aparelhos MIDI abertos (hubs e outros aparelhos nao contam).
int deviceCount();

// Incrementa a cada Note On repassado entre aparelhos (para piscar o LED).
uint32_t routedNoteCount();

}  // namespace usb_midi_host
