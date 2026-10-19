#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

int64_t nowMs();
bool getNum(const json& j, const char* key, double& out);
int decimalsOf(const std::string& s);
bool validSymbol(const std::string& s);
double niceStep(double raw);
void fmtTime(int64_t ms, const char* f, char* out, size_t n);
void fmtDuration(int64_t ms, char* out, size_t n);
void fmtCompact(double v, char* out, size_t n);
void notifyDesktop(const std::string& title, const std::string& body);
