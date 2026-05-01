#include "http_ctrl_usage_monitor.h"
#include "http_ctrl_globals.h"

#include <stdio.h>
#include <stdint.h>

#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <cctype>

#include "rknnPool.hpp"  // g_opencv_draw_total_us, g_opencv_draw_sample_count

// ---------------------- CPU usage ----------------------

bool read_cpu_times(CpuTimes* out) {
    if (!out) return false;
    std::ifstream ifs("/proc/stat");
    if (!ifs.is_open()) return false;
    std::string line;
    if (!std::getline(ifs, line)) return false;
    std::istringstream iss(line);
    std::string cpu_tag;
    iss >> cpu_tag;
    if (cpu_tag != "cpu") return false;
    CpuTimes t;
    iss >> t.user >> t.nice >> t.system >> t.idle >> t.iowait >> t.irq >> t.softirq >> t.steal;
    *out = t;
    return true;
}

double calc_cpu_usage_percent(const CpuTimes& prev, const CpuTimes& cur) {
    const uint64_t idle_prev = prev.idle + prev.iowait;
    const uint64_t idle_cur = cur.idle + cur.iowait;
    const uint64_t non_idle_prev = prev.user + prev.nice + prev.system + prev.irq + prev.softirq + prev.steal;
    const uint64_t non_idle_cur = cur.user + cur.nice + cur.system + cur.irq + cur.softirq + cur.steal;
    const uint64_t total_prev = idle_prev + non_idle_prev;
    const uint64_t total_cur = idle_cur + non_idle_cur;
    if (total_cur <= total_prev) return -1.0;
    const double totald = (double)(total_cur - total_prev);
    const double idled = (double)(idle_cur - idle_prev);
    return std::max(0.0, std::min(100.0, (totald - idled) * 100.0 / totald));
}

// ---------------------- NPU usage ----------------------

double parse_npu_load_percent_text(const std::string& text) {
    std::vector<double> values;
    values.reserve(8);

    // 优先解析带 % 的格式，兼容如 "core0: 35%"
    for (size_t i = 0; i < text.size();) {
        if (!std::isdigit((unsigned char)text[i])) {
            ++i;
            continue;
        }
        size_t start = i;
        while (i < text.size() && std::isdigit((unsigned char)text[i])) ++i;
        if (i < text.size() && text[i] == '.') {
            ++i;
            while (i < text.size() && std::isdigit((unsigned char)text[i])) ++i;
        }
        if (i < text.size() && text[i] == '%') {
            values.push_back(strtod(text.substr(start, i - start).c_str(), nullptr));
            ++i;
        }
    }

    // 回退：不带 % 的纯数字输出（只取 0..100）
    if (values.empty()) {
        for (size_t i = 0; i < text.size();) {
            if (!std::isdigit((unsigned char)text[i])) {
                ++i;
                continue;
            }
            size_t start = i;
            while (i < text.size() && std::isdigit((unsigned char)text[i])) ++i;
            long v = strtol(text.substr(start, i - start).c_str(), nullptr, 10);
            if (v >= 0 && v <= 100) {
                values.push_back((double)v);
            }
        }
    }

    if (values.empty()) return -1.0;
    double sum = 0.0;
    for (double v : values) sum += v;
    return sum / (double)values.size();
}

double read_npu_load_percent() {
    std::ifstream ifs("/sys/kernel/debug/rknpu/load");
    if (!ifs.is_open()) return -1.0;
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (content.empty()) return -1.0;
    return parse_npu_load_percent_text(content);
}

// ---------------------- Usage monitor loop ----------------------

void usage_monitor_loop() {
    const auto sample_interval = std::chrono::seconds(1);
    const auto report_window = std::chrono::seconds(10);
    auto next_report = std::chrono::steady_clock::now() + report_window;

    CpuTimes prev_cpu;
    bool has_prev_cpu = read_cpu_times(&prev_cpu);
    bool npu_warned = false;
    std::vector<double> cpu_samples;
    std::vector<double> npu_samples;
    cpu_samples.reserve(16);
    npu_samples.reserve(16);
    long long cv_draw_total_us_window = 0;
    long long cv_draw_samples_window = 0;

    while (g_running.load()) {
        std::this_thread::sleep_for(sample_interval);
        if (!g_running.load()) break;

        CpuTimes cur_cpu;
        if (read_cpu_times(&cur_cpu)) {
            if (has_prev_cpu) {
                double cpu = calc_cpu_usage_percent(prev_cpu, cur_cpu);
                if (cpu >= 0.0) cpu_samples.push_back(cpu);
            }
            prev_cpu = cur_cpu;
            has_prev_cpu = true;
        }

        double npu = read_npu_load_percent();
        if (npu >= 0.0) {
            npu_samples.push_back(npu);
        } else if (!npu_warned) {
            printf("[Usage] 警告: 无法读取 NPU 负载(/sys/kernel/debug/rknpu/load)\n");
            npu_warned = true;
        }

        long long cv_sum_us = g_opencv_draw_total_us.exchange(0, std::memory_order_relaxed);
        long long cv_cnt = g_opencv_draw_sample_count.exchange(0, std::memory_order_relaxed);
        if (cv_sum_us > 0 && cv_cnt > 0) {
            cv_draw_total_us_window += cv_sum_us;
            cv_draw_samples_window += cv_cnt;
        }

        auto now = std::chrono::steady_clock::now();
        if (now >= next_report) {
            auto calc_avg = [](const std::vector<double>& samples) -> double {
                if (samples.empty()) return -1.0;
                double s = 0.0;
                for (double v : samples) s += v;
                return s / (double)samples.size();
            };
            double cpu_avg = calc_avg(cpu_samples);
            double npu_avg = calc_avg(npu_samples);
            double cv_draw_avg_ms = (cv_draw_samples_window > 0)
                                        ? (double)cv_draw_total_us_window / (double)cv_draw_samples_window / 1000.0
                                        : -1.0;
            auto metric_percent = [](double value) -> std::string {
                if (value < 0.0) return "N/A";
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f%%", value);
                return std::string(buf);
            };
            auto metric_ms = [](double value) -> std::string {
                if (value < 0.0) return "N/A";
                char buf[32];
                snprintf(buf, sizeof(buf), "%.2fms/帧", value);
                return std::string(buf);
            };
            std::string cpu_text = metric_percent(cpu_avg);
            std::string npu_text = metric_percent(npu_avg);
            std::string cv_text = metric_ms(cv_draw_avg_ms);
            printf("[Usage-10s] CPU平均: %s | NPU平均: %s | OpenCV绘制: %s\n",
                   cpu_text.c_str(), npu_text.c_str(), cv_text.c_str());
            cpu_samples.clear();
            npu_samples.clear();
            cv_draw_total_us_window = 0;
            cv_draw_samples_window = 0;
            next_report = now + report_window;
        }
    }
}
