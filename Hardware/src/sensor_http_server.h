#pragma once

#include "ads1115_module.h"

#include <atomic>
#include <ctime>
#include <string>

constexpr int kHttpPort = 8088;
constexpr int kPollIntervalMs = 1000;

extern std::atomic<bool> g_running;

void updateSnapshot(const SensorSnapshot& snapshot);
SensorSnapshot getSnapshotCopy();
void* pollingThreadMain(void*);
void daemonize();

std::string buildServiceInfoJson();
std::string buildHealthJson();
std::string buildSensorJson(const SensorSnapshot& snapshot);
int queueResponse(struct MHD_Connection* connection,
                  unsigned int status_code,
                  const std::string& body,
                  const char* content_type = "application/json");
int requestHandler(void* cls,
                   struct MHD_Connection* connection,
                   const char* url,
                   const char* method,
                   const char* version,
                   const char* upload_data,
                   size_t* upload_data_size,
                   void** con_cls);
