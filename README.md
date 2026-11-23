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
| `s3zero_router` | **Host USB + roteador**: vários aparelhos MIDI no hub, o que um envia os outros recebem | `src/router/` + `src/common/` |
| `s3zero_device` | Dispositivo USB MIDI (aparece como teclado no PC, celular ou no `s3zero_router`) | `src/device/` |
| `s3zero_harpa_vl53l1x` / `s3zero_harpa_vl53l0x` | **Harpa laser**: 10 sensores (um por corda), host (toca no FM1) ou device (teclado MIDI) escolhido no boot por uma ponte de solda | `src/harpa/` + `lib/harpa/` + `src/role/` |
| `s3zero_modetest` | Teste da escolha host/device (ponte em GPIO5/6), sem sensores | `src/modetest/` |
| `serial_bridge` | Ferramenta de desenvolvimento: adaptador USB-serial, para ler o log do `s3zero_host` | `src/serial_bridge/` |
| `native` | Testes da lógica no PC, sem hardware | `test/` |

A lógica comum (debounce do botão, sequência de notas e a interface
`IMidiOut`) fica em `lib/core/` e é usada por todos os perfis.

```
pio run -e s3zero_host -t upload
pio test -e native -e native_l0x
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

## Harpa laser (`s3zero_harpa_vl53l1x` e `s3zero_harpa_vl53l0x`)

Cada sensor de distância é uma corda: distância abaixo do `trigger` envia
Note On, acima do `release` envia Note Off. Há um perfil por tipo de sensor
(a placa montada tem um só tipo).

**Estado dos testes:** a lógica passa nos testes do PC para os dois sensores.
No hardware, só foi validado o lado USB (host com o FM1 e a escolha
host/device pela ponte, via `s3zero_modetest`). A leitura dos sensores, nos
dois tipos, ainda não foi testada com sensores ligados.

**Host ou device, no boot:** o mesmo binário serve aos dois papéis. Uma ponte
de solda entre os pads vizinhos **GPIO42 e GPIO45** (lado de baixo) decide, e
só a pilha USB escolhida é iniciada:

| Ponte 42–45 | Modo |
|---|---|
| sem ponte | **Host**: toca no FM1, como o `s3zero_host` |
| com ponte | **Device**: aparece como teclado MIDI "Harpa MIDI" |

A ponte só é lida no boot (mude e aperte RESET). No boot o LED mostra o modo
escolhido por 0,7 s: **ciano** = device, **laranja** = host. O mesmo mecanismo,
em GPIO5 e GPIO6, é o `s3zero_modetest`: o botão BOOT toca a sequência de teste
nos dois modos e dá para validar sem sensores.

Tudo que se edita fica em **`lib/harpa/harpa_config.h`**, e erros de
configuração quebram a compilação:

- `SENSOR_CONFIGS`: uma linha por corda (XSHUT, endereço I2C, ROI, trigger,
  release). O número de cordas é o tamanho da tabela (máximo 16). Trigger e
  release padrão: 800/850 mm no VL53L1X, 600/650 mm no VL53L0X (valor do
  projeto antigo; ajuste na calibração). O ROI só vale para o VL53L1X.
- Escala: `ROOT_NOTE` e `SCALE_INTERVALS`. O padrão é Rendez-Vous II
  (`0 5 7 8 10 11 12 14 15 17 19 20`) a partir de C3 (nota 48); só os primeiros
  intervalos, até o número de cordas, são usados.
- Validações: pino ou endereço repetido, 0x29 como endereço final, GPIO fora do
  que a S3-Zero oferece, XSHUT igual a um pino do I2C ou da ponte, ROI
  inválida, release ≤ trigger, escala curta demais, sensor não definido.

**Ligação** (diagrama em `images/ESP32-S3-Zero-Pinout.png`): SDA = GPIO1,
SCL = GPIO2 (400 kHz), XSHUT nos GPIO 4 a 13, nessa ordem, com endereços 0x30
a 0x39. Os pinos 0, 3, 19, 20, 21, 43 e 44 ficam livres de propósito (BOOT,
strapping, USB, LED, UART0). Não ligue XSHUT a 3,3 V: o
firmware o mantém em reset e depois o solta como entrada, deixando o pull-up
do módulo conduzir.

**BOOT e LED:** o botão BOOT toca a sequência de teste, como nos outros
perfis, mesmo que nenhum sensor tenha iniciado. O LED segue as cores do
`s3zero_host`, com uma a mais: **ciano** quando o FM1 está pronto mas faltam
sensores (verde só com todos no ar). Branco = corda ou botão tocando.

**Latência:** cada volta do loop lê só os sensores que têm amostra nova (sem
nunca esperar por medição; no VL53L0X, que só tem leitura bloqueante na
biblioteca, o registrador de dado pronto é consultado antes) e começa a consultar cada um poucos ms antes do fim
do período de 20 ms. A distância fica guardada no cache de cada corda para o
uso futuro como expressão.

**Sensores ausentes:** um sensor que não sobe no boot, ou que fica 1 s sem
amostras, é reiniciado a cada 5 s, um por vez e só com todas as cordas soltas,
porque a tentativa bloqueia o loop por até ~100 ms. Os pinos e prazos estão no
mesmo arquivo de configuração.

**Log** (UART0, 115200): `Lidars available: N/10`, uma linha por sensor, e
`String 3 PRESSED` / `String 3 RELEASED` a cada mudança. Com
`-DHARPA_TELEMETRY=1` no perfil, uma linha `DIST` a cada 100 ms:

```
DIST 0:742:6:VALID:ACTIVE 1:X:0:INVALID:IDLE ...
```

Cada campo é `índice:distância_mm_ou_X:idade_ms:VALID|STALE|INVALID:ACTIVE|IDLE`.
`STALE` mantém a última distância; `X` aparece só quando a medida é inválida.

## LED (`s3zero_host`)

O brilho vale para todos os perfis e se ajusta no `platformio.ini`, em
`[esp32s3_zero].led_flags`: `LED_LEVEL` é o brilho máximo (0..255) e
`LED_GREEN_PCT` reduz só o verde (0..100), que ao olho parece mais forte.

| Cor | Significado |
|---|---|
| Magenta | O host USB não iniciou |
| Azul | Nenhum dispositivo USB |
| Amarelo | Algum dispositivo enumerado (por exemplo, só o hub), mas nenhum MIDI |
| Verde | FM1 pronto |
| Branco | Nota tocando |

## Roteador (`s3zero_router`)

Ainda não testado no hardware. A montagem é a mesma do `s3zero_host`, com mais
aparelhos MIDI no hub (teclado, pads, knobs):

- **Regra:** o que chega de um aparelho vai para todos os outros, nunca de volta
  para quem enviou. O botão BOOT toca a sequência de teste para todos.
- **Filtros padrão** (`lib/core/midi_router.h`):
  - só o cabo virtual 0;
  - só mensagens de canal (nota, CC, program change, pitch bend, aftertouch),
    então SysEx, clock e active sensing são descartados;
  - canal forçado para 1, o canal de notas padrão do FM1. Mude com
    `-DROUTER_FORCE_CHANNEL=N`, ou use `0` para manter o canal original.
- **Limites:**
  - o código aceita até 4 aparelhos MIDI, mas o hardware limita antes;
  - o ESP32-S3 tem **8 canais de hardware USB para tudo** (hubs e aparelhos
    que não são MIDI inclusive). Quanto cada item ocupa não foi medido;
  - **prefira um hub simples.** Resultados com FM1 + um segundo aparelho MIDI
    (uma S3-Zero com o perfil `s3zero_device`):

    | Hub | Resultado |
    |---|---|
    | Dock com 2 chips de hub e placa de rede | ✘ só o primeiro aparelho MIDI abre |
    | Hub simples (1 hub) com porta de rede | ✔ os dois abrem e o roteamento funciona |

  - Se um aparelho não abrir, o log pode mostrar `falha ao abrir interface`,
    ou o aparelho nem chega a aparecer no `[enum]`.
- **Loop:** se dois aparelhos reenviam o que recebem (MIDI thru), as mensagens
  podem circular entre eles.

| Cor do LED | Significado |
|---|---|
| Magenta | O host USB não iniciou |
| Azul | Nenhum aparelho MIDI |
| Amarelo | Um aparelho MIDI (nada para rotear ainda) |
| Verde | Dois ou mais, prontos para rotear |
| Vermelho | Roteando: aceso enquanto chegam mensagens (notas, knobs, pitch bend) de um aparelho para outro, e até 150 ms depois da última |
| Branco | Nota do botão BOOT tocando |

Com `-DUSB_HOST_DIAG=1`, cada pacote roteado aparece no log como
`[midi] origem -> máscara de destinos: bytes`.

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
