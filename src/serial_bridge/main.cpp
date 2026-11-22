// Ferramenta de desenvolvimento (perfil serial_bridge), nao faz parte do produto.
// Ponte USB <-> UART: transforma um ESP32-S3 Zero num adaptador USB-serial
// para ler o log da placa do perfil s3zero_host.
//
// Ligacao (cruzada), entre as duas placas:
//   ponte GPIO43 (TX)  -> alvo GPIO44 (RX)
//   ponte GPIO44 (RX)  <- alvo GPIO43 (TX)
//   GND                -- GND
// Nao ligue 5V/3V3 entre elas: cada placa tem sua alimentacao.
#include <Arduino.h>

#ifndef BRIDGE_BAUD
#define BRIDGE_BAUD 115200
#endif
#ifndef BRIDGE_RX_PIN
#define BRIDGE_RX_PIN 44
#endif
#ifndef BRIDGE_TX_PIN
#define BRIDGE_TX_PIN 43
#endif

// Serial = USB (CDC do USB-Serial/JTAG), Serial1 = UART nos pinos acima.
void setup() {
  Serial.begin(BRIDGE_BAUD);
  Serial1.setRxBufferSize(4096);
  Serial1.begin(BRIDGE_BAUD, SERIAL_8N1, BRIDGE_RX_PIN, BRIDGE_TX_PIN);
  rgbLedWrite(RGB_BUILTIN, 0, 0, 20);  // azul fraco: ponte ligada
}

void loop() {
  uint8_t buf[256];
  size_t n = Serial1.available();
  if (n) {
    n = Serial1.readBytes(buf, min(n, sizeof(buf)));
    Serial.write(buf, n);
  }
  n = Serial.available();
  if (n) {
    n = Serial.readBytes(buf, min(n, sizeof(buf)));
    Serial1.write(buf, n);
  }
}
