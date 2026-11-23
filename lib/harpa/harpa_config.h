#pragma once
#include <stddef.h>
#include <stdint.h>

// Configuracao da harpa laser (perfil s3zero_host_harpa_vl53l1x). TUDO que se edita fica
// neste arquivo; as validacoes abaixo quebram a compilacao se algo estiver
// errado (pino ou endereco repetido, ROI invalida, histerese invalida etc.).
//
// Pinos pensados para a Waveshare ESP32-S3-Zero rodando como HOST USB:
//   - GPIO19/20 sao o USB, GPIO43/44 sao a UART0 (log), GPIO21 e o LED,
//     GPIO0 e o botao BOOT e GPIO3 e pino de strapping: nao usar.
//   - O I2C fica em GPIO1 (SDA) e GPIO2 (SCL); os XSHUT, em GPIO4..13.
//   Diagrama dos pinos: images/ESP32-S3-Zero-Pinout.png

struct RoiConfig {
  uint8_t width;    // 4..16 SPADs
  uint8_t height;   // 4..16 SPADs
  uint8_t centerX;  // 0..15
  uint8_t centerY;  // 0..15
};

struct SensorConfig {
  uint8_t xshutPin;     // GPIO ligado ao XSHUT do sensor
  uint8_t i2cAddress;   // endereco final (0x29 e o de fabrica, nao pode)
  RoiConfig roi;
  uint16_t triggerMm;   // abaixo disso a corda esta PRESSED
  uint16_t releaseMm;   // acima disso volta a RELEASED (> triggerMm)
};

// ---------------------------------------------------------------------------
// Barramento I2C
// ---------------------------------------------------------------------------
constexpr uint8_t I2C_SDA_PIN = 1;
constexpr uint8_t I2C_SCL_PIN = 2;
constexpr uint32_t I2C_CLOCK_HZ = 400000;

// ---------------------------------------------------------------------------
// Sensores: uma linha por corda. O numero de cordas e o tamanho desta tabela
// (maximo 16). Para 12 sensores, acrescente 2 linhas e a escala abaixo
// precisa ter 12 intervalos.
// ---------------------------------------------------------------------------
//                       XSHUT  I2C   ROI{w,h,cx,cy}  trigger release
constexpr SensorConfig SENSOR_CONFIGS[] = {
    {4,  0x30, {4, 4, 8, 7}, 800, 850},
    {5,  0x31, {4, 4, 8, 7}, 800, 850},
    {6,  0x32, {4, 4, 8, 7}, 800, 850},
    {7,  0x33, {4, 4, 8, 7}, 800, 850},
    {8,  0x34, {4, 4, 8, 7}, 800, 850},
    {9,  0x35, {4, 4, 8, 7}, 800, 850},
    {10, 0x36, {4, 4, 8, 7}, 800, 850},
    {11, 0x37, {4, 4, 8, 7}, 800, 850},
    {12, 0x38, {4, 4, 8, 7}, 800, 850},
    {13, 0x39, {4, 4, 8, 7}, 800, 850},
};
constexpr size_t SENSOR_COUNT = sizeof(SENSOR_CONFIGS) / sizeof(SENSOR_CONFIGS[0]);

// ---------------------------------------------------------------------------
// Medicao e cache
// ---------------------------------------------------------------------------
constexpr uint32_t MEASUREMENT_BUDGET_US = 20000;  // tempo de uma medida
constexpr uint32_t MEASUREMENT_PERIOD_MS = 20;     // >= budget; menor = menos latencia
constexpr uint32_t SENSOR_TIMEOUT_MS = 100;        // timeout da lib no boot
constexpr uint32_t STALE_AFTER_MS = 100;           // cache mais velho que isso e STALE
// Sensor que nao entrega nenhuma amostra por este tempo e dado como falho e
// volta para a fila de nova tentativa.
constexpr uint32_t DEAD_AFTER_MS = 1000;
// Intervalo entre tentativas de reiniciar sensores ausentes/falhos. A
// tentativa bloqueia o loop por ate ~100 ms, por isso so ocorre com todas as
// cordas soltas.
constexpr uint32_t RETRY_INTERVAL_MS = 5000;

// ---------------------------------------------------------------------------
// MIDI: cada corda toca ROOT_NOTE + SCALE_INTERVALS[corda].
// Escala padrao: Rendez-Vous II (Jean-Michel Jarre), a partir de C3 (48,
// com C4 = 60). Os intervalos sao os mesmos do preset do laser-46 e so os
// primeiros SENSOR_COUNT sao usados.
// ---------------------------------------------------------------------------
constexpr uint8_t ROOT_NOTE = 48;
constexpr uint8_t SCALE_INTERVALS[] = {0, 5, 7, 8, 10, 11, 12, 14, 15, 17, 19, 20};
constexpr uint8_t MIDI_CHANNEL = 1;  // 1..16
constexpr uint8_t NOTE_VELOCITY = 100;  // fixa por enquanto

// ---------------------------------------------------------------------------
// Validacoes (funcoes constexpr, tambem usadas nos testes)
// ---------------------------------------------------------------------------
namespace harpa_cfg {

// Pino livre na S3-Zero: GPIO1..18, sem o 3 (strapping). Os 19/20 (USB),
// 21 (LED), 0 (BOOT), 43/44 (UART0) e 26..37 (flash/PSRAM) ficam de fora.
constexpr bool isUsableGpio(uint8_t pin) { return pin >= 1 && pin <= 18 && pin != 3; }

constexpr bool isValidRoi(const RoiConfig &roi) {
  if (roi.width < 4 || roi.width > 16 || roi.height < 4 || roi.height > 16 ||
      roi.centerX > 15 || roi.centerY > 15) {
    return false;
  }
  const uint8_t left = roi.width / 2;
  const uint8_t right = roi.width - left - 1;
  const uint8_t top = (roi.height - 1) / 2;
  const uint8_t bottom = roi.height - top - 1;
  return roi.centerX >= left && roi.centerX + right < 16 && roi.centerY >= top &&
         roi.centerY + bottom < 16;
}

// Numero de SPAD para o centro do ROI, no formato da lib Pololu / ST.
constexpr uint8_t spadNumber(uint8_t x, uint8_t y) {
  return y < 8 ? static_cast<uint8_t>(128 + x * 8 + y)
               : static_cast<uint8_t>(127 - x * 8 - (y - 8));
}

constexpr bool sensorIsValid(const SensorConfig &c) {
  return isUsableGpio(c.xshutPin) && c.xshutPin != I2C_SDA_PIN && c.xshutPin != I2C_SCL_PIN &&
         c.i2cAddress >= 0x08 && c.i2cAddress <= 0x77 && c.i2cAddress != 0x29 &&
         isValidRoi(c.roi) && c.releaseMm > c.triggerMm;
}

constexpr bool sensorsAreValid(const SensorConfig *c, size_t n) {
  if (n == 0 || n > 16) return false;
  for (size_t i = 0; i < n; ++i) {
    if (!sensorIsValid(c[i])) return false;
    for (size_t j = 0; j < i; ++j) {
      if (c[i].xshutPin == c[j].xshutPin || c[i].i2cAddress == c[j].i2cAddress) return false;
    }
  }
  return true;
}

// A escala precisa cobrir todas as cordas e cada nota ficar em 0..127.
constexpr bool scaleIsValid(uint8_t root, const uint8_t *intervals, size_t intervalCount,
                            size_t strings) {
  if (intervalCount < strings) return false;
  for (size_t i = 0; i < strings; ++i) {
    if (root + intervals[i] > 127) return false;
  }
  return true;
}

constexpr uint8_t noteForString(size_t index) {
  return static_cast<uint8_t>(ROOT_NOTE + SCALE_INTERVALS[index]);
}

}  // namespace harpa_cfg

static_assert(harpa_cfg::sensorsAreValid(SENSOR_CONFIGS, SENSOR_COUNT),
              "SENSOR_CONFIGS invalido: pino/endereco repetido ou fora do permitido, "
              "ROI invalida, ou release <= trigger");
static_assert(harpa_cfg::scaleIsValid(ROOT_NOTE, SCALE_INTERVALS,
                                      sizeof(SCALE_INTERVALS) / sizeof(SCALE_INTERVALS[0]),
                                      SENSOR_COUNT),
              "SCALE_INTERVALS precisa ter ao menos SENSOR_COUNT intervalos e notas <= 127");
static_assert(MIDI_CHANNEL >= 1 && MIDI_CHANNEL <= 16, "MIDI_CHANNEL deve ser 1..16");
static_assert(MEASUREMENT_PERIOD_MS * 1000 >= MEASUREMENT_BUDGET_US,
              "MEASUREMENT_PERIOD_MS deve ser >= MEASUREMENT_BUDGET_US");
