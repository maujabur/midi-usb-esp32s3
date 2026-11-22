# midi-usb-esp32s3

ESP32-S3 (Waveshare ESP32-S3-Zero) enviando MIDI por USB para um M-Vave FM1.
O botão BOOT toca uma sequência de teste: Note On ao apertar, Note Off ao
soltar, avançando uma nota por toque.

O FM1 é um dispositivo USB e só aceita MIDI vindo de um host, como um
computador ou um celular. Por isso o perfil principal, `s3zero_host`, coloca o
ESP32-S3 no papel de host USB.

## Perfis (`platformio.ini`)

| Perfil | Papel do S3 | Código |
|---|---|---|
| `s3zero_host` | **Host USB**: enumera o FM1 e envia MIDI | `src/host/` |
| `s3zero` | Dispositivo USB MIDI (aparece como teclado no PC) | `src/device/` |
| `serial_bridge` | Ferramenta de desenvolvimento: adaptador USB-serial, para ler o log do `s3zero_host` | `src/serial_bridge/` |
| `native` | Testes da lógica no PC, sem hardware | `test/` |

A lógica comum (debounce do botão, sequência de notas e a interface
`IMidiOut`) fica em `lib/core/` e é usada por todos os perfis.

```
pio run -e s3zero_host -t upload
pio test -e native
```

Depois do primeiro upload do `s3zero_host`, a porta USB passa a ser host e o PC
não enxerga mais a placa. Para gravar de novo, segure BOOT, aperte RESET e
solte BOOT.

## Montagem do `s3zero_host`

Testado e funcionando:

```
fonte PD ── hub ── cabo C-C ── S3-Zero (sem modificação)
             └──── FM1
```

- O FM1 também funciona ligado direto na S3-Zero, com cabo C-C e sem hub.
- O hub do teste foi um dock com dois chips de hub em cascata e uma placa de
  rede. O código ignora o que não é MIDI.
- Conectar e desconectar o FM1 com tudo ligado funciona.
- **Alimentação:**
  - Com o hub PD, o hub alimenta a S3-Zero pelo próprio cabo, e alimenta também
    o FM1.
  - Sem hub, alimente a S3-Zero pelo pino 5V. O VBUS da placa é ligado direto
    ao pino 5V, sem diodo, então os 5 V chegam ao FM1.
  - Use **uma fonte de 5 V só**. Como não há diodo, duas fontes no mesmo VBUS
    (por exemplo, hub PD e o 5V da ponte serial) ficam ligadas entre si.

### Placa de diagnóstico A/B (opcional)

Durante a investigação foi montado um par de breakouts USB-C, ligados por
quatro fios (V, G, D−, D+). Na placa B, os resistores de 5,1 kΩ dos pinos CC
foram trocados por 56 kΩ de V para CC1 e de V para CC2, para que ela se
apresente como host/fonte no USB-C.

- **Não é necessária**: o FM1 aceita a S3-Zero como host mesmo com os 5,1 kΩ
  originais da placa.
- **Atrapalha com o hub PD**: o hub vê os 56 kΩ (sinal de fonte), não manda
  energia de volta e a S3-Zero não liga.
- **Serve para diagnóstico**: dá acesso a D+, D−, VBUS e CC para medir com o
  multímetro. Com o FM1 conectado e se anunciando, D+ fica perto de 3 V e o CC
  ativo perto de 0,42 V.

## LED (`s3zero_host`)

| Cor | Significado |
|---|---|
| Magenta | O host USB não iniciou |
| Azul | Nenhum dispositivo USB |
| Amarelo | Algum dispositivo enumerado (por exemplo, só o hub), mas nenhum MIDI |
| Verde | FM1 pronto |
| Branco | Nota tocando |

## Log e ponte serial

O `s3zero_host` escreve o log na UART0, a 115200. Sem um adaptador USB-serial,
use uma segunda S3-Zero com o perfil `serial_bridge`:

| Ponte | Alvo (`s3zero_host`) |
|---|---|
| GPIO43 (TX) | GPIO44 (RX) |
| GPIO44 (RX) | GPIO43 (TX) |
| GND | GND |

Não ligue 5V entre as placas se o alvo já tiver outra fonte (veja
Alimentação).

```
pio run -e serial_bridge -t upload
pio device monitor -e serial_bridge
```

Log normal com hub:

```
[host] boot
[host] host USB iniciado, aguardando dispositivos
[enum] VID=05e3 PID=0610 classe=09/00/00 usb=0210
[enum] VID=214b PID=7250 classe=09/00/00 usb=0200
[enum] VID=0bda PID=8153 classe=00/00/00 usb=0210
[usb-host] 0bda:8153 nao e MIDI, ignorado
[enum] VID=4c4a PID=c755 classe=00/00/00 usb=0110
[usb-host] MIDI pronto: 4c4a:c755 interface 4, EP OUT 0x04
```

O FM1 aparece como `4c4a:c755`, com o MIDI na interface 4 e o endpoint bulk
OUT `0x04`.

Para logs detalhados, mude `-DUSB_HOST_DIAG=0` para `1` no perfil
`s3zero_host`. Eles mostram cada mudança na porta USB (conexão, reset,
habilitação, linhas D+/D−) e, a cada 5 s, os registradores do controlador OTG e
do PHY.

## Notas técnicas

- **`enum_filter_cb` é obrigatório.** O core Arduino-ESP32 (ESP-IDF 5.5) vem
  compilado com `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y`. Sem registrar
  esse callback no `usb_host_install`, a porta era resetada e habilitada, mas a
  enumeração nunca terminava e nenhum erro aparecia. Registrar o callback foi a
  única mudança funcional entre a versão que falhava e a que funcionou. A causa
  exata no IDF não foi confirmada.
- **Suporte a hub já vem no core**: `CONFIG_USB_HOST_HUBS_SUPPORTED=y`, com
  vários níveis de hub. O código não precisa de componente extra.
- **Logs do IDF** vêm compilados só no nível ERROR
  (`CONFIG_LOG_MAXIMUM_LEVEL=1`). Avisos da pilha USB não aparecem, por isso
  existe o diagnóstico próprio (`USB_HOST_DIAG`).
- O S3 só fala USB full-speed. O FM1 é full-speed (`usb=0110`).
