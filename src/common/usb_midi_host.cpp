#include "usb_midi_host.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include <usb/usb_host.h>

namespace {

constexpr uint8_t SUBCLASS_MIDISTREAMING = 0x03;
constexpr int RING_LEN = 64;  // pacotes de 4 bytes enfileirados por destino

struct Dev {
  bool used;
  bool closing;   // removido: esperando as transferencias voltarem
  bool inBusy;    // leitura (IN) submetida
  bool outBusy;   // escrita (OUT) submetida
  uint8_t iface;
  uint16_t vid, pid;
  usb_device_handle_t handle;
  usb_transfer_t *in;   // nullptr se o aparelho nao envia MIDI
  usb_transfer_t *out;  // nullptr se o aparelho nao recebe MIDI
  uint16_t outMps;
  uint32_t ring[RING_LEN];
  int head, tail;
};

Dev g_devs[usb_midi_host::MAX_DEVICES];
usb_host_client_handle_t g_client = nullptr;
QueueHandle_t g_localQueue = nullptr;  // pacotes de origem local (uint32_t)
RouterConfig g_cfg;

volatile int g_deviceCount = 0;
volatile uint32_t g_routedCount = 0;

// Eventos chegam no callback; abrir/fechar e feito depois, no loop da task
// (a lib nao permite isso de forma reentrante).
constexpr int MAX_PENDING = 8;
uint8_t g_pendingNew[MAX_PENDING];
int g_pendingNewCount = 0;
usb_device_handle_t g_pendingGone[MAX_PENDING];
int g_pendingGoneCount = 0;

int indexOf(const Dev &d) { return static_cast<int>(&d - g_devs); }

void updateCount() {
  int n = 0;
  for (auto &d : g_devs) n += (d.used && !d.closing) ? 1 : 0;
  g_deviceCount = n;
}

uint32_t outCapableMask() {
  uint32_t m = 0;
  for (auto &d : g_devs) {
    if (d.used && !d.closing && d.out) m |= 1u << indexOf(d);
  }
  return m;
}

// Envia o que couber da fila numa transferencia OUT, se ela estiver livre.
void kick(Dev &d) {
  if (!d.out || d.outBusy || d.closing || d.head == d.tail) return;
  int n = 0;
  const int maxPackets = d.outMps / 4;
  while (d.tail != d.head && n < maxPackets) {
    memcpy(d.out->data_buffer + n * 4, &d.ring[d.tail], 4);
    d.tail = (d.tail + 1) % RING_LEN;
    n++;
  }
  d.out->num_bytes = n * 4;
  d.outBusy = usb_host_transfer_submit(d.out) == ESP_OK;
}

void enqueue(Dev &d, const uint8_t p[4]) {
  const int next = (d.head + 1) % RING_LEN;
  if (next == d.tail) return;  // fila cheia: descarta em vez de travar o USB
  memcpy(&d.ring[d.head], p, 4);
  d.head = next;
}

// Coracao do roteador: filtra e entrega a todos os destinos.
void route(int source, const uint8_t raw[4]) {
  uint8_t p[4];
  memcpy(p, raw, 4);
  if (!midi_router::filter(g_cfg, p)) return;
  const uint32_t mask = midi_router::destinations(outCapableMask(), source);
#if USB_HOST_DIAG
  Serial.printf("[midi] %d -> 0x%02lx: %02x %02x %02x %02x\n", source, (unsigned long)mask, p[0],
                p[1], p[2], p[3]);
#endif
  for (auto &d : g_devs) {
    if (!(mask & (1u << indexOf(d)))) continue;
    enqueue(d, p);
    kick(d);
  }
  if (source != midi_router::LOCAL_SOURCE && mask)
    g_routedCount = g_routedCount + 1;  // so esta task escreve
}

void outDone(usb_transfer_t *t) {
  Dev &d = *static_cast<Dev *>(t->context);
  d.outBusy = false;
  kick(d);
}

void inDone(usb_transfer_t *t) {
  Dev &d = *static_cast<Dev *>(t->context);
  if (t->status == USB_TRANSFER_STATUS_COMPLETED) {
    for (int i = 0; i + 4 <= t->actual_num_bytes; i += 4) route(indexOf(d), t->data_buffer + i);
  }
  // Continua lendo ate o aparelho sair. Qualquer status que nao seja
  // "completo" (remocao, cancelamento, stall) encerra a leitura deste aparelho.
  if (!d.closing && t->status == USB_TRANSFER_STATUS_COMPLETED &&
      usb_host_transfer_submit(t) == ESP_OK)
    return;
  d.inBusy = false;
}

// Primeira interface MIDIStreaming com endpoint bulk IN e/ou OUT.
struct MidiEndpoints {
  uint8_t iface = 0, alt = 0;
  const usb_ep_desc_t *in = nullptr;
  const usb_ep_desc_t *out = nullptr;
};

bool findMidiEndpoints(const usb_config_desc_t *cfg, MidiEndpoints &r) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(cfg);
  const uint8_t *end = p + cfg->wTotalLength;
  p += cfg->bLength;
  bool inMidi = false;
  while (p + 2 <= end && p[0] >= 2) {
    if (p[1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      if (r.in || r.out) return true;  // ja achamos na interface anterior
      auto *i = reinterpret_cast<const usb_intf_desc_t *>(p);
      inMidi = i->bInterfaceClass == USB_CLASS_AUDIO &&
               i->bInterfaceSubClass == SUBCLASS_MIDISTREAMING;
      r.iface = i->bInterfaceNumber;
      r.alt = i->bAlternateSetting;
    } else if (p[1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT && inMidi) {
      auto *e = reinterpret_cast<const usb_ep_desc_t *>(p);
      const bool bulk =
          (e->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK;
      const bool dirIn = (e->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
      if (bulk && dirIn && !r.in) r.in = e;
      if (bulk && !dirIn && !r.out) r.out = e;
    }
    p += p[0];
  }
  return r.in || r.out;
}

usb_transfer_t *makeTransfer(Dev &d, const usb_ep_desc_t *ep, usb_transfer_cb_t cb) {
  const uint16_t mps = ep->wMaxPacketSize & 0x7FF;
  usb_transfer_t *t;
  if (usb_host_transfer_alloc(mps, 0, &t) != ESP_OK) return nullptr;
  t->device_handle = d.handle;
  t->bEndpointAddress = ep->bEndpointAddress;
  t->callback = cb;
  t->context = &d;
  t->num_bytes = mps;  // leitura IN precisa ser multipla do MPS
  return t;
}

void openDevice(uint8_t addr) {
  Dev *slot = nullptr;
  for (auto &d : g_devs) {
    if (!d.used) {
      slot = &d;
      break;
    }
  }

  usb_device_handle_t h;
  if (usb_host_device_open(g_client, addr, &h) != ESP_OK) return;
  const usb_device_desc_t *dd;
  usb_host_get_device_descriptor(h, &dd);
  const usb_config_desc_t *cfg;
  MidiEndpoints ep;
  if (usb_host_get_active_config_descriptor(h, &cfg) != ESP_OK || !findMidiEndpoints(cfg, ep)) {
    Serial.printf("[midi] %04x:%04x nao e MIDI, ignorado\n", dd->idVendor, dd->idProduct);
    usb_host_device_close(g_client, h);
    return;
  }
  if (!slot) {
    Serial.printf("[midi] %04x:%04x ignorado: limite de %d aparelhos MIDI\n", dd->idVendor,
                  dd->idProduct, usb_midi_host::MAX_DEVICES);
    usb_host_device_close(g_client, h);
    return;
  }
  // Reivindicar a interface aloca os canais de hardware dos endpoints. O
  // ESP32-S3 tem 8 canais para TODOS os aparelhos (hubs inclusive).
  esp_err_t err = usb_host_interface_claim(g_client, h, ep.iface, ep.alt);
  if (err != ESP_OK) {
    Serial.printf("[midi] %04x:%04x falha ao abrir interface %u: %s\n", dd->idVendor,
                  dd->idProduct, ep.iface, esp_err_to_name(err));
    usb_host_device_close(g_client, h);
    return;
  }

  memset(slot, 0, sizeof(*slot));
  slot->used = true;
  slot->handle = h;
  slot->iface = ep.iface;
  slot->vid = dd->idVendor;
  slot->pid = dd->idProduct;
  if (ep.out) {
    slot->out = makeTransfer(*slot, ep.out, outDone);
    slot->outMps = ep.out->wMaxPacketSize & 0x7FF;
  }
  if (ep.in) {
    slot->in = makeTransfer(*slot, ep.in, inDone);
    if (slot->in) slot->inBusy = usb_host_transfer_submit(slot->in) == ESP_OK;
  }
  updateCount();
  Serial.printf("[midi] aberto %04x:%04x (aparelho %d, interface %u, envia=%s recebe=%s)\n",
                slot->vid, slot->pid, indexOf(*slot), ep.iface, slot->in ? "sim" : "nao",
                slot->out ? "sim" : "nao");
}

void startClosing(usb_device_handle_t h) {
  for (auto &d : g_devs) {
    if (!d.used || d.handle != h || d.closing) continue;
    d.closing = true;
    // Cancela as transferencias pendentes; os callbacks limpam os flags.
    for (usb_transfer_t *t : {d.in, d.out}) {
      if (!t) continue;
      usb_host_endpoint_halt(h, t->bEndpointAddress);
      usb_host_endpoint_flush(h, t->bEndpointAddress);
    }
    Serial.printf("[midi] removido %04x:%04x (aparelho %d)\n", d.vid, d.pid, indexOf(d));
  }
  updateCount();
}

// Termina de fechar quem ja teve as transferencias devolvidas.
void reapClosed() {
  for (auto &d : g_devs) {
    if (!d.used || !d.closing || d.inBusy || d.outBusy) continue;
    for (usb_transfer_t *t : {d.in, d.out}) {
      if (!t) continue;
      usb_host_endpoint_clear(d.handle, t->bEndpointAddress);
      usb_host_transfer_free(t);
    }
    usb_host_interface_release(g_client, d.handle, d.iface);
    usb_host_device_close(g_client, d.handle);
    d.used = false;
  }
}

void clientEventCb(const usb_host_client_event_msg_t *msg, void *) {
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    if (g_pendingNewCount < MAX_PENDING) g_pendingNew[g_pendingNewCount++] = msg->new_dev.address;
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    if (g_pendingGoneCount < MAX_PENDING) g_pendingGone[g_pendingGoneCount++] = msg->dev_gone.dev_hdl;
  }
}

// Chamado pela lib durante a enumeracao. Registrar este callback e o que faz a
// enumeracao funcionar neste core (ver begin()).
bool enumFilterCb(const usb_device_desc_t *d, uint8_t *) {
  Serial.printf("[enum] VID=%04x PID=%04x classe=%02x/%02x/%02x usb=%04x\n", d->idVendor,
                d->idProduct, d->bDeviceClass, d->bDeviceSubClass, d->bDeviceProtocol, d->bcdUSB);
  return true;
}

void libTask(void *) {
  for (;;) {
    uint32_t flags = 0;
    esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (err != ESP_OK) Serial.printf("[usb-lib] handle_events: %s\n", esp_err_to_name(err));
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

void clientTask(void *) {
  for (;;) {
    // Timeout curto: os pacotes locais e o fechamento de aparelhos removidos
    // sao tratados mesmo sem eventos USB. Callbacks IN/OUT rodam aqui dentro.
    usb_host_client_handle_events(g_client, 1);
    for (int i = 0; i < g_pendingGoneCount; i++) startClosing(g_pendingGone[i]);
    g_pendingGoneCount = 0;
    for (int i = 0; i < g_pendingNewCount; i++) openDevice(g_pendingNew[i]);
    g_pendingNewCount = 0;
    uint32_t pkt;
    while (xQueueReceive(g_localQueue, &pkt, 0) == pdTRUE) {
      route(midi_router::LOCAL_SOURCE, reinterpret_cast<const uint8_t *>(&pkt));
    }
    reapClosed();
  }
}

}  // namespace

namespace usb_midi_host {

bool begin(const RouterConfig &cfg) {
  g_cfg = cfg;
  g_localQueue = xQueueCreate(32, sizeof(uint32_t));
  if (!g_localQueue) return false;

  usb_host_config_t hostCfg = {};
  hostCfg.skip_phy_setup = false;
  hostCfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  // NAO REMOVER: o core vem com CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y e,
  // sem este callback, a enumeracao nunca terminava (ver README).
  hostCfg.enum_filter_cb = enumFilterCb;
  esp_err_t err = usb_host_install(&hostCfg);
  if (err != ESP_OK) {
    Serial.printf("[usb-host] usb_host_install: %s\n", esp_err_to_name(err));
    return false;
  }

  usb_host_client_config_t clientCfg = {};
  clientCfg.is_synchronous = false;
  clientCfg.max_num_event_msg = 8;
  clientCfg.async.client_event_callback = clientEventCb;
  clientCfg.async.callback_arg = nullptr;
  err = usb_host_client_register(&clientCfg, &g_client);
  if (err != ESP_OK) {
    Serial.printf("[usb-host] usb_host_client_register: %s\n", esp_err_to_name(err));
    return false;
  }

  if (xTaskCreate(libTask, "usbh_lib", 4096, nullptr, 20, nullptr) != pdPASS) return false;
  return xTaskCreate(clientTask, "usbh_midi", 4096, nullptr, 19, nullptr) == pdPASS;
}

void sendLocal(const uint8_t packet[4]) {
  if (!g_localQueue) return;
  uint32_t pkt;
  memcpy(&pkt, packet, 4);
  xQueueSend(g_localQueue, &pkt, 0);
}

int deviceCount() { return g_deviceCount; }

uint32_t routedPacketCount() { return g_routedCount; }

}  // namespace usb_midi_host
