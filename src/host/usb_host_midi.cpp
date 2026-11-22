#include "usb_host_midi.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>
#include <usb/usb_host.h>

#if USB_HOST_DIAG
#include <soc/usb_dwc_struct.h>
#include <soc/usb_serial_jtag_reg.h>
#include <soc/usb_wrap_reg.h>
#endif

namespace {

constexpr uint8_t SUBCLASS_MIDISTREAMING = 0x03;

usb_host_client_handle_t g_client = nullptr;
usb_device_handle_t g_dev = nullptr;
usb_transfer_t *g_xfer = nullptr;
QueueHandle_t g_queue = nullptr;  // pacotes USB-MIDI de 4 bytes (uint32_t)

uint8_t g_ifaceNum = 0;
uint8_t g_epOut = 0;
volatile bool g_connected = false;
volatile int g_busDevices = 0;    // dispositivos no barramento (inclui hubs)
volatile bool g_busy = false;     // transferencia em andamento

// Eventos chegam no callback; abrir/fechar e feito depois, no loop da task
// (a lib nao permite isso de forma reentrante). Com hub, varios NEW_DEV podem
// chegar juntos (o hub e o FM1), entao guardamos uma lista.
constexpr int MAX_PENDING = 8;
uint8_t g_pendingNew[MAX_PENDING];
int g_pendingNewCount = 0;
bool g_closeReq = false;          // o dispositivo MIDI aberto foi removido

// Procura interface MIDIStreaming e seu endpoint bulk OUT na config descriptor.
bool findMidiOut(const usb_config_desc_t *cfg, uint8_t &iface, uint8_t &alt, uint8_t &ep) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(cfg);
  const uint8_t *end = p + cfg->wTotalLength;
  p += cfg->bLength;
  bool inMidi = false;
  uint8_t curIface = 0, curAlt = 0;
  while (p + 2 <= end && p[0] >= 2) {
    if (p[1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      auto *i = reinterpret_cast<const usb_intf_desc_t *>(p);
      inMidi = i->bInterfaceClass == USB_CLASS_AUDIO &&
               i->bInterfaceSubClass == SUBCLASS_MIDISTREAMING;
      curIface = i->bInterfaceNumber;
      curAlt = i->bAlternateSetting;
    } else if (p[1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT && inMidi) {
      auto *e = reinterpret_cast<const usb_ep_desc_t *>(p);
      bool bulk = (e->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK;
      bool out = (e->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) == 0;
      if (bulk && out) {
        iface = curIface;
        alt = curAlt;
        ep = e->bEndpointAddress;
        return true;
      }
    }
    p += p[0];
  }
  return false;
}

void xferCb(usb_transfer_t *) { g_busy = false; }

void closeDevice() {
  g_connected = false;
  if (g_dev) {
    usb_host_interface_release(g_client, g_dev, g_ifaceNum);
    usb_host_device_close(g_client, g_dev);
    g_dev = nullptr;
  }
  g_busy = false;
  xQueueReset(g_queue);
  Serial.println("[usb-host] dispositivo fechado");
}

void openDevice(uint8_t addr) {
  usb_device_handle_t dev;
  if (usb_host_device_open(g_client, addr, &dev) != ESP_OK) return;

  const usb_device_desc_t *dd;
  usb_host_get_device_descriptor(dev, &dd);

  const usb_config_desc_t *cfg;
  uint8_t iface = 0, alt = 0, ep = 0;
  if (usb_host_get_active_config_descriptor(dev, &cfg) != ESP_OK ||
      !findMidiOut(cfg, iface, alt, ep)) {
    Serial.printf("[usb-host] %04x:%04x nao e MIDI, ignorado\n", dd->idVendor, dd->idProduct);
    usb_host_device_close(g_client, dev);
    return;
  }
  if (usb_host_interface_claim(g_client, dev, iface, alt) != ESP_OK) {
    Serial.println("[usb-host] falha ao reivindicar interface");
    usb_host_device_close(g_client, dev);
    return;
  }
  g_dev = dev;
  g_ifaceNum = iface;
  g_epOut = ep;
  g_busy = false;
  xQueueReset(g_queue);
  g_connected = true;
  Serial.printf("[usb-host] MIDI pronto: %04x:%04x interface %u, EP OUT 0x%02x\n", dd->idVendor,
                dd->idProduct, iface, ep);
}

void clientEventCb(const usb_host_client_event_msg_t *msg, void *) {
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
#if USB_HOST_DIAG
    Serial.printf("[usb-host] NEW_DEV endereco %u\n", msg->new_dev.address);
#endif
    if (g_pendingNewCount < MAX_PENDING) g_pendingNew[g_pendingNewCount++] = msg->new_dev.address;
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    // So fecha se foi o nosso dispositivo (com hub, outros podem sair).
    if (g_dev && msg->dev_gone.dev_hdl == g_dev) g_closeReq = true;
  }
}

volatile uint32_t g_libEvents = 0;  // diagnostico: chamadas a lib que retornaram

void libTask(void *) {
  for (;;) {
    uint32_t flags = 0;
    esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &flags);
    g_libEvents = g_libEvents + 1;
    if (err != ESP_OK) Serial.printf("[usb-lib] handle_events: %s\n", esp_err_to_name(err));
#if USB_HOST_DIAG
    if (flags) Serial.printf("[usb-lib] flags=0x%lx\n", (unsigned long)flags);
#endif
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

// Chamado pela lib no meio da enumeracao, logo apos ler o device descriptor.
// Registrar este callback e o que faz a enumeracao funcionar (ver begin()).
bool enumFilterCb(const usb_device_desc_t *d, uint8_t *bConfigurationValue) {
  Serial.printf("[enum] VID=%04x PID=%04x classe=%02x/%02x/%02x usb=%04x\n", d->idVendor,
                d->idProduct, d->bDeviceClass, d->bDeviceSubClass, d->bDeviceProtocol,
                d->bcdUSB);
  return true;  // aceita o dispositivo, com a configuracao padrao
}

#if USB_HOST_DIAG
// Registra toda mudanca na porta raiz (conexao, reset, habilitacao).
void logPortChanges() {
  static uint32_t last = 0xFFFFFFFF;
  uint32_t hprt = USB_DWC.hprt_reg.val & ((1u << 0) | (1u << 2) | (3u << 10) | (1u << 12) | (1u << 8) | (3u << 17));
  if (hprt == last) return;
  last = hprt;
  Serial.printf("[porta] t=%lu conn=%lu ena=%lu reset=%lu D+=%lu D-=%lu pwr=%lu spd=%lu\n",
                (unsigned long)millis(), (unsigned long)(hprt & 1), (unsigned long)((hprt >> 2) & 1),
                (unsigned long)((hprt >> 8) & 1), (unsigned long)((hprt >> 10) & 1),
                (unsigned long)((hprt >> 11) & 1), (unsigned long)((hprt >> 12) & 1),
                (unsigned long)((hprt >> 17) & 3));
}
#endif

void clientTask(void *) {
  uint32_t lastPoll = 0;
  for (;;) {
    usb_host_client_handle_events(g_client, pdMS_TO_TICKS(5));

    if (g_closeReq) {
      g_closeReq = false;
      closeDevice();
    }
    for (int i = 0; i < g_pendingNewCount; i++) {
      if (!g_dev) openDevice(g_pendingNew[i]);
    }
    g_pendingNewCount = 0;

#if USB_HOST_DIAG
    logPortChanges();
#endif

    // Quantos dispositivos a lib enumerou (hub incluso): usado pelo LED amarelo.
    uint32_t now = millis();
    if (now - lastPoll >= 200) {
      lastPoll = now;
      uint8_t addrs[16];
      int n = 0;
      if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &n) == ESP_OK) {
#if USB_HOST_DIAG
        if (n != g_busDevices) Serial.printf("[usb-host] dispositivos no barramento: %d\n", n);
#endif
        g_busDevices = n;
      }
    }
    if (g_connected && !g_busy) {
      uint32_t pkt;
      if (xQueueReceive(g_queue, &pkt, 0) == pdTRUE) {
        memcpy(g_xfer->data_buffer, &pkt, 4);
        g_xfer->num_bytes = 4;
        g_xfer->device_handle = g_dev;
        g_xfer->bEndpointAddress = g_epOut;
        g_xfer->callback = xferCb;
        g_xfer->context = nullptr;
        g_busy = true;
        if (usb_host_transfer_submit(g_xfer) != ESP_OK) g_busy = false;
      }
    }
  }
}

}  // namespace

bool UsbHostMidiOut::begin() {
  g_queue = xQueueCreate(32, sizeof(uint32_t));
  if (!g_queue) return false;

  usb_host_config_t hostCfg = {};
  hostCfg.skip_phy_setup = false;
  hostCfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  // NAO REMOVER: o core vem com CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y.
  // Sem este callback a porta ficava habilitada mas a enumeracao nunca
  // terminava; com ele o FM1 passou a enumerar. Unica mudanca funcional entre
  // a versao que falhava e a que funcionou (causa exata nao confirmada).
  hostCfg.enum_filter_cb = enumFilterCb;
  esp_err_t err = usb_host_install(&hostCfg);
  if (err != ESP_OK) {
    Serial.printf("[usb-host] usb_host_install: %s\n", esp_err_to_name(err));
    return false;
  }

  usb_host_client_config_t clientCfg = {};
  clientCfg.is_synchronous = false;
  clientCfg.max_num_event_msg = 5;
  clientCfg.async.client_event_callback = clientEventCb;
  clientCfg.async.callback_arg = nullptr;
  err = usb_host_client_register(&clientCfg, &g_client);
  if (err != ESP_OK) {
    Serial.printf("[usb-host] usb_host_client_register: %s\n", esp_err_to_name(err));
    return false;
  }
  if (usb_host_transfer_alloc(64, 0, &g_xfer) != ESP_OK) return false;

  xTaskCreate(libTask, "usbh_lib", 4096, nullptr, 20, nullptr);
  xTaskCreate(clientTask, "usbh_client", 4096, nullptr, 19, nullptr);
  return true;
}

bool UsbHostMidiOut::connected() const { return g_connected; }

bool UsbHostMidiOut::deviceSeen() const { return g_busDevices > 0; }

// Diagnostico de baixo nivel: estado da porta do controlador OTG e de quem
// esta com o PHY interno (OTG ou USB-Serial/JTAG). So leitura de registradores.
void UsbHostMidiOut::dumpDiag() const {
#if USB_HOST_DIAG
  uint32_t hprt = USB_DWC.hprt_reg.val;
  Serial.printf("[diag] HPRT=0x%08lx conn=%lu ena=%lu linha(D+=%lu D-=%lu) pwr=%lu spd=%lu\n",
                (unsigned long)hprt, (unsigned long)(hprt & 1), (unsigned long)((hprt >> 2) & 1),
                (unsigned long)((hprt >> 10) & 1), (unsigned long)((hprt >> 11) & 1),
                (unsigned long)((hprt >> 12) & 1), (unsigned long)((hprt >> 17) & 3));
  Serial.printf("[diag] GOTGCTL=0x%08lx GUSBCFG=0x%08lx GINTSTS=0x%08lx\n",
                (unsigned long)USB_DWC.gotgctl_reg.val, (unsigned long)USB_DWC.gusbcfg_reg.val,
                (unsigned long)USB_DWC.gintsts_reg.val);
  uint32_t wrap = REG_READ(USB_WRAP_OTG_CONF_REG);
  Serial.printf("[diag] WRAP=0x%08lx phy_sel=%lu pad_en=%lu pull_ovr=%lu dp_pu=%lu dp_pd=%lu dm_pd=%lu\n",
                (unsigned long)wrap, (unsigned long)((wrap >> 2) & 1), (unsigned long)((wrap >> 18) & 1),
                (unsigned long)((wrap >> 12) & 1), (unsigned long)((wrap >> 13) & 1),
                (unsigned long)((wrap >> 14) & 1), (unsigned long)((wrap >> 16) & 1));
  uint32_t usj = REG_READ(USB_SERIAL_JTAG_CONF0_REG);
  Serial.printf("[diag] USJ=0x%08lx phy_sel=%lu pad_en=%lu pull_ovr=%lu dp_pu=%lu\n",
                (unsigned long)usj, (unsigned long)(usj & 1), (unsigned long)((usj >> 14) & 1),
                (unsigned long)((usj >> 8) & 1), (unsigned long)((usj >> 9) & 1));
  usb_host_lib_info_t info = {};
  if (usb_host_lib_info(&info) == ESP_OK)
    Serial.printf("[diag] lib: devices=%d clients=%d eventos=%lu\n", info.num_devices,
                  info.num_clients, (unsigned long)g_libEvents);
#endif
}

void UsbHostMidiOut::send(uint8_t status, uint8_t d1, uint8_t d2) {
  if (!g_connected || !g_queue) return;
  // Pacote USB-MIDI: [cable<<4 | CIN, status, d1, d2]. CIN = nibble alto do status.
  uint8_t b[4] = {static_cast<uint8_t>(status >> 4), status, d1, d2};
  uint32_t pkt;
  memcpy(&pkt, b, 4);
  xQueueSend(g_queue, &pkt, 0);
}

void UsbHostMidiOut::noteOn(uint8_t note, uint8_t velocity, uint8_t channel) {
  send(0x90 | ((channel - 1) & 0x0F), note & 0x7F, velocity & 0x7F);
}

void UsbHostMidiOut::noteOff(uint8_t note, uint8_t velocity, uint8_t channel) {
  send(0x80 | ((channel - 1) & 0x0F), note & 0x7F, velocity & 0x7F);
}
