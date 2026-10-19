#pragma once

#include "imgui.h"

inline const ImU32 COL_BG   = IM_COL32(10, 12, 16, 255);
inline const ImU32 COL_AXIS = IM_COL32(18, 21, 27, 255);
inline const ImU32 COL_GRID = IM_COL32(32, 37, 46, 255);
inline const ImU32 COL_TXT  = IM_COL32(170, 176, 188, 255);
inline const ImU32 COL_UP   = IM_COL32(0, 190, 90, 255);
inline const ImU32 COL_DN   = IM_COL32(230, 50, 55, 255);
inline const ImU32 COL_SMA  = IM_COL32(255, 170, 0, 255);

void drawChart(const ImVec2& p0, const ImVec2& size);
void drawScreener(const ImVec2& size);
