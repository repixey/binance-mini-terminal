#pragma once

#include <cstdint>
#include <string>

#include "types.h"

void wakeMain();
void setStatus(const std::string& s);
void cancelHttp();
void fetchKlines(int gen, const std::string& sym, const std::string& api,
                 int64_t endTime, Kind kind, int limit);
void pushLive(const LiveEvent& ev);
void restartWs(int gen, bool refill);
