#ifndef HTTP_CTRL_USAGE_MONITOR_H
#define HTTP_CTRL_USAGE_MONITOR_H

// CPU/NPU usage monitoring functions extracted from main_http_ctrl.cc.
// CpuTimes struct is defined in http_ctrl_globals.h.

#include "http_ctrl_globals.h"

bool read_cpu_times(CpuTimes* out);
double calc_cpu_usage_percent(const CpuTimes& prev, const CpuTimes& cur);

double parse_npu_load_percent_text(const std::string& text);
double read_npu_load_percent();

void usage_monitor_loop();

#endif // HTTP_CTRL_USAGE_MONITOR_H
