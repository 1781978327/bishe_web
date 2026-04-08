/**
 * MQ-2 烟雾传感器读取（香橙派 5 + ADS1115 AIN0）
 *
 * 接线（ADS1115 AIN0）：
 *   MQ-2 VCC  → 5V
 *   MQ-2 GND  → GND
 *   MQ-2 AOUT → ADS1115 AIN0
 *   MQ-2 DOUT →（本程序不用，悬空或接 GPIO）
 *
 * 电路模型（电压分压）：
 *   5V ──┬── MQ-2 (Rs) ──┬── AIN0 ── ADS1115
 *        │              │
 *        └─── 10kΩ ─────┘
 *
 * ADS1115 配置：单端 AIN0，量程 ±4.096V，128 SPS
 *
 * 编译：
 *   g++ -std=c++17 -O2 -Wall mq2_read.cpp -o mq2_read
 * 运行：
 *   sudo ./mq2_read
 */

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

// ─── ADS1115 寄存器与常量 ────────────────────────────────────────────────────
static constexpr uint8_t REG_POINTER_CONVERT = 0x00;
static constexpr uint8_t REG_POINTER_CONFIG  = 0x01;

static constexpr uint16_t OS_SINGLE    = 0x8000;
static constexpr uint16_t MUX_SINGLE_0 = 0x4000;   // AIN0 vs GND
static constexpr uint16_t PGA_4_096V   = 0x0200;
static constexpr uint16_t MODE_SINGLE  = 0x0100;
static constexpr uint16_t DR_128SPS    = 0x0080;
static constexpr uint16_t COMP_QUE_NONE = 0x0003;

// 系统电压（实际用万用表量 5V 引脚得到更准确）
static constexpr float VCC = 5.0f;
// 负载电阻（Ω），MQ-2 电压分压电路中串联的那个电阻
static constexpr float RL = 10000.0f;
// ADS1115 满量程电压（与 PGA 设置一致）
static constexpr float FS_VOLTS = 4.096f;
// ADS1115 码值转电压系数
static constexpr float ADC_SCALE = FS_VOLTS / 32768.0f;

// ─── ADS1115 I2C 操作 ─────────────────────────────────────────────────────────
static inline bool i2cWriteReg16(int fd, uint8_t reg, uint16_t value) {
    uint8_t buf[3] = { reg, static_cast<uint8_t>(value >> 8),
                       static_cast<uint8_t>(value & 0xFF) };
    return ::write(fd, buf, 3) == 3;
}

static bool i2cReadConversion(int fd, int16_t& out) {
    uint8_t ptr = REG_POINTER_CONVERT;
    if (::write(fd, &ptr, 1) != 1) return false;
    uint8_t data[2];
    if (::read(fd, data, 2) != 2) return false;
    out = static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) |
                               static_cast<uint16_t>(data[1]));
    return true;
}

/** 读 AIN0 单端转换值（阻塞约 8ms） */
static bool readAds1115Channel0(int fd, int16_t& raw) {
    const uint16_t config =
        OS_SINGLE | MUX_SINGLE_0 | PGA_4_096V | MODE_SINGLE | DR_128SPS | COMP_QUE_NONE;
    if (!i2cWriteReg16(fd, REG_POINTER_CONFIG, config))
        return false;
    ::usleep(9000);   // 128 SPS 转换时间约 7.8ms，留点余量
    return i2cReadConversion(fd, raw);
}

// ─── MQ-2 电压 → 烟雾浓度换算 ────────────────────────────────────────────────
/**
 * MQ-2 传感器电阻计算（分压电路）：
 *   Vout = VCC * RL / (RL + Rs)   →   Rs = RL * (VCC - Vout) / Vout
 *
 * 清洁空气参考浓度：
 *   传感器数据手册给的清洁空气中 Ro ~= 9.83 kΩ（典型值，不同批次有差异）
 *   这里把首次测量值作为动态 Ro，后续以 Ro 比率换算更贴合实际。
 */
struct MQ2Sensor {
    float vo;     // 当前测量电压 (V)
    float rs;     // 当前传感器电阻 (Ω)
    float ro;     // 清洁空气基准电阻 (Ω)，首次测量后固定
    bool  roFixed;

    MQ2Sensor() : vo(0), rs(0), ro(0), roFixed(false) {}

    void update(float voltage) {
        vo = voltage;
        // 防止除零
        if (vo <= 0.001f) { rs = 1e6f; return; }
        rs = RL * (VCC - vo) / vo;

        if (!roFixed) {
            ro = rs;   // 首次测量作为清洁空气基准
            roFixed = true;
        }
    }

    // MQ-2 / MQ-7 典型特性曲线近似：ratio = Rs/Ro
    // ratio 越大 → 气体浓度越低；ratio 越小 → 浓度越高
    float ratio() const { return ro > 0 ? rs / ro : 0.0f; }

    /** 返回估计的 LPG 浓度（ppm），仅供参考 */
    float lpg_ppm() const {
        // 手册数据点近似拟合: ppm = 1000 * pow(ratio, -1.55)  (500~10000 ppm 区间)
        float r = ratio();
        if (r <= 0) return 0;
        if (r > 20) r = 20;  // 限幅，防止 pow 溢出
        return 1000.0f * std::pow(r, -1.55f);
    }

    /** 返回估计的烟雾（smoke）相对浓度 (%)，0~100% */
    float smoke_percent() const {
        // 映射：ratio=Ro（清洁）→ 0%；ratio→0（高浓度）→ 100%
        float r = ratio();
        if (r <= 0)  return 100.0f;
        if (r >= 1.0f) return 0.0f;   // ratio >= 1 → 清洁空气，0%
        return (1.0f - r) * 100.0f;
    }

    /** 简化为 0~100 整数警报级别（可按需调阈值） */
    int alert_level() const {
        float pct = smoke_percent();
        if (pct > 80) return 3;  // 危险
        if (pct > 50) return 2;  // 警告
        if (pct > 20) return 1;  // 轻微
        return 0;                // 安全
    }
};

static const char* LEVEL_STR[] = { "Safe", "Low", "Warn", "DANGER" };

// ─── 主程序 ───────────────────────────────────────────────────────────────────
int main() {
    constexpr const char* I2C_DEV     = "/dev/i2c-1";
    constexpr int         ADC_ADDR    = 0x48;

    int fd = ::open(I2C_DEV, O_RDWR);
    if (fd < 0) {
        std::perror("open /dev/i2c-1");
        std::printf("提示：请确认设备树已启用 I2C1，且运行在 root 或 i2c 组权限下\n");
        return 1;
    }
    if (::ioctl(fd, I2C_SLAVE, ADC_ADDR) < 0) {
        std::perror("ioctl I2C_SLAVE");
        ::close(fd);
        return 1;
    }

    std::printf("╔══════════════════════════════════════════╗\n");
    std::printf("║  MQ-2 烟雾传感器读取                       ║\n");
    std::printf("║  ADS1115 AIN0 | I2C @ 0x%02X | ±4.096V    ║\n", ADC_ADDR);
    std::printf("║  RL = %.0fΩ  |  VCC = %.1fV              ║\n", RL, VCC);
    std::printf("╚══════════════════════════════════════════╝\n");
    std::printf("首次测量自动标定为 Ro（清洁空气基准）\n");
    std::printf("  Ctrl+C 退出\n\n");

    MQ2Sensor mq2;

    for (int iter = 0;; ++iter) {
        int16_t raw;
        if (!readAds1115Channel0(fd, raw)) {
            std::printf("[ERR] I2C 读取失败: %s\n", std::strerror(errno));
            ::usleep(500000);
            continue;
        }

        float voltage = static_cast<float>(raw) * ADC_SCALE;
        mq2.update(voltage);

        // 仅在 Ro 未固定（首次）时提示
        if (!mq2.roFixed) {
            std::printf("[INFO] 等待 Ro 标定中，请确保传感器预热 30s 以上...\n");
            ::usleep(250000);
            continue;
        }

        float ratio = mq2.ratio();
        float ppm   = mq2.lpg_ppm();
        float pct   = mq2.smoke_percent();
        int   lvl   = mq2.alert_level();

        std::printf(
            "\r[AIN0] V=%.3fV  Ro=%.0fΩ  Rs=%.0fΩ  ratio=%.2f  LPG≈%.0fppm  "
            "smoke=%.0f%%  [%s]     ",
            voltage, mq2.ro, mq2.rs, ratio, ppm, pct, LEVEL_STR[lvl]);
        std::fflush(stdout);

        ::usleep(250000);
    }

    ::close(fd);
    return 0;
}
