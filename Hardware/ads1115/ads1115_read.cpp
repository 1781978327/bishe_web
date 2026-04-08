/**
 * ADS1115 读取（香橙派 5）
 *
 * 说明：香橙派上 wiringPi 的 wiringPiI2CReadReg16 对 ADS1115 常返回 -1（读失败），
 * 故本程序改用标准 Linux I2C 字符设备 /dev/i2c-1（你已用 i2cdetect -y 1 扫到 0x48）。
 *
 * 编译（无需 -lwiringPi）：
 *   g++ -std=c++17 -O2 -Wall ads1115_read.cpp -o ads1115_read
 * 运行：
 *   sudo ./ads1115_read
 */

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

// ADS1115 寄存器指针
static constexpr uint8_t REG_POINTER_CONVERT = 0x00;
static constexpr uint8_t REG_POINTER_CONFIG = 0x01;

static constexpr uint16_t OS_SINGLE = 0x8000;
static constexpr uint16_t MUX_SINGLE_0 = 0x4000;
static constexpr uint16_t MUX_SINGLE_1 = 0x5000;
static constexpr uint16_t MUX_SINGLE_2 = 0x6000;
static constexpr uint16_t MUX_SINGLE_3 = 0x7000;
static constexpr uint16_t PGA_4_096V = 0x0200;
static constexpr uint16_t MODE_SINGLE = 0x0100;
static constexpr uint16_t DR_128SPS = 0x0080;
static constexpr uint16_t COMP_QUE_NONE = 0x0003;

static inline uint16_t muxSingle(uint8_t ch) {
  switch (ch & 3) {
    case 0: return MUX_SINGLE_0;
    case 1: return MUX_SINGLE_1;
    case 2: return MUX_SINGLE_2;
    default: return MUX_SINGLE_3;
  }
}

/** 向 ADS1115 写「寄存器指针 + 16 位数据」，MSB 在前（与 TI 手册一致） */
static bool i2cWriteReg16(int fd, uint8_t reg, uint16_t value) {
  uint8_t buf[3] = {reg, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
  const ssize_t n = ::write(fd, buf, sizeof(buf));
  return n == static_cast<ssize_t>(sizeof(buf));
}

/** 先写指针寄存器，再读 2 字节，MSB 在前 → 有符号 16 位 */
static bool i2cReadConversion(int fd, int16_t& out) {
  uint8_t ptr = REG_POINTER_CONVERT;
  if (::write(fd, &ptr, 1) != 1) {
    return false;
  }
  uint8_t data[2];
  if (::read(fd, data, 2) != 2) {
    return false;
  }
  out = static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
  return true;
}

static bool readAds1115SingleEnded(int fd, uint8_t ch, int16_t& raw) {
  const uint16_t config = static_cast<uint16_t>(
      OS_SINGLE | muxSingle(ch) | PGA_4_096V | MODE_SINGLE | DR_128SPS | COMP_QUE_NONE);

  if (!i2cWriteReg16(fd, REG_POINTER_CONFIG, config)) {
    return false;
  }
  // 128 SPS 典型转换时间约 8ms
  ::usleep(9000);

  return i2cReadConversion(fd, raw);
}

int main() {
  constexpr const char* I2C_DEV = "/dev/i2c-1";
  constexpr int ADC_ADDRESS = 0x48;

  const int fd = ::open(I2C_DEV, O_RDWR);
  if (fd < 0) {
    std::perror("open /dev/i2c-1");
    std::printf("若不存在，请确认设备树已启用 I2C1，且路径为 /dev/i2c-1\n");
    return 1;
  }

  if (::ioctl(fd, I2C_SLAVE, ADC_ADDRESS) < 0) {
    std::perror("ioctl I2C_SLAVE");
    ::close(fd);
    return 1;
  }

  std::printf("ADS1115 on %s @ 0x%02X, AIN0~AIN3 single-ended, PGA=±4.096V\n", I2C_DEV, ADC_ADDRESS);
  std::printf("Ctrl+C to quit.\n\n");
  std::printf("  raw: | %6s | %6s | %6s | %6s |\n", "AIN0", "AIN1", "AIN2", "AIN3");
  std::printf("  Vin: | %6s | %6s | %6s | %6s | (V at pin, FS=±4.096V)\n", "V", "V", "V", "V");
  std::printf("-------+--------+--------+--------+--------+\n");

  for (;;) {
    int16_t raw[4] = {};
    bool ok = true;
    for (int ch = 0; ch < 4; ++ch) {
      if (!readAds1115SingleEnded(fd, static_cast<uint8_t>(ch), raw[ch])) {
        ok = false;
        break;
      }
    }
    if (!ok) {
      std::printf("I2C read/write failed: %s\n", std::strerror(errno));
      ::usleep(250000);
      continue;
    }

    // 单端正电压：码值换算到引脚电压（与 ±4.096V 满量程一致）
    constexpr float fs_volts = 4.096f;
    constexpr float scale = fs_volts / 32768.0f;
    float vin[4];
    for (int i = 0; i < 4; ++i) {
      vin[i] = static_cast<float>(raw[i]) * scale;
    }

    std::printf("  raw | %6d | %6d | %6d | %6d |\n", static_cast<int>(raw[0]),
                static_cast<int>(raw[1]), static_cast<int>(raw[2]), static_cast<int>(raw[3]));
    std::printf("  Vin | %6.3f | %6.3f | %6.3f | %6.3f |\n", vin[0], vin[1], vin[2], vin[3]);

    // 仅 AIN0：沿用你原来的电池分压公式（169k+9.31k)/9.31k —— 若未接分压可忽略该行
    const float vbat = static_cast<float>(raw[0]) * 4.096f / static_cast<float>(0x7FFF) *
                       (169.0f + 9.31f) / 9.31f;
    std::printf("  vbat (from AIN0 divider) = %.2f V\n", vbat);

    ::usleep(250000);
  }

  ::close(fd);
  return 0;
}
