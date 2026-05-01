#pragma once

void *rt_monitor_thread(void *arg);
int rt_start(const char *device);
int rt_stop();
