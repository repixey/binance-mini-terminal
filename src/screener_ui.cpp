#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "screener.h"
#include "session.h"
#include "state.h"
#include "ui.h"
#include "util.h"

void drawScreener(const ImVec2& size) {
    const float ui = ImGui::GetFontSize() / 13.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 sp = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(sp, sp + size, COL_AXIS);

    if (!g_scrOpen) {
        if (ImGui::Button(">", ImVec2(size.x, 0))) g_scrOpen = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Screener");
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_AXIS);
    ImGui::BeginChild("##scr", size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::Indent(5 * ui);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3 * ui);

    if (ImGui::SmallButton("<")) g_scrOpen = false;
    ImGui::SameLine();
    ImGui::TextUnformatted("Screener");

    const int curIv = g_scr.interval();
    for (int i = 0; i < N_INTERVALS; ++i) {
        if (i) ImGui::SameLine(0, 2);
        const bool active = i == curIv;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.80f, 1.0f));
        if (ImGui::SmallButton(INTERVALS[i].label) && !active) g_scr.setInterval(i);
        if (active) ImGui::PopStyleColor();
    }

    static char addBuf[32] = "";
    ImGui::SetNextItemWidth(size.x - 14 * ui);
    if (ImGui::InputTextWithHint("##scradd", "add ticker + Enter", addBuf, sizeof addBuf,
                                 ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank |
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        std::string s = addBuf;
        auto ends = [&](const char* suf) {
            const size_t n = std::strlen(suf);
            return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
        };
        if (!s.empty() && !ends("USDT") && !ends("USDC")) s += "USDT";
        g_scr.add(s);
        addBuf[0] = 0;
    }

    {
        static char alBuf[16] = "";
        ImGui::SetNextItemWidth(size.x - 14 * ui);
        const bool ent = ImGui::InputTextWithHint("##scralert", "alert SMA %: +5 / -3, empty = off",
                                                  alBuf, sizeof alBuf,
                                                  ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
        if (ent || ImGui::IsItemDeactivatedAfterEdit()) {
            char* end = nullptr;
            const double v = std::strtod(alBuf, &end);
            g_scr.setAlert(alBuf[0] && end != alBuf ? v : 0.0);
        }
        const double ap = g_scr.alert();
        if (ap != 0) ImGui::TextDisabled("alert: %s %.2f%%", ap > 0 ? "SMA >=" : "SMA <=", ap);
        else         ImGui::TextDisabled("alert: off");
    }

    std::vector<ScrItem> rows = g_scr.snapshot();
    ImGui::TextDisabled("%d pairs | click: chart | RMB: remove", (int)rows.size());

    const float th = std::max(40.0f * ui, ImGui::GetContentRegionAvail().y - 2 * ui);
    const ImGuiTableFlags tf = ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_SizingStretchProp;
    std::string toRemove;
    if (ImGui::BeginTable("scr", 4, tf, ImVec2(0, th))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Ticker", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Vol 24h", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort |
                                           ImGuiTableColumnFlags_PreferSortDescending, 0.9f);
        ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort, 1.1f);
        ImGui::TableSetupColumn("SMA %", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortDescending, 0.9f);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
            if (ss->SpecsCount > 0) {
                const int col = ss->Specs[0].ColumnIndex;
                const bool asc = ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                std::stable_sort(rows.begin(), rows.end(), [&](const ScrItem& a, const ScrItem& b) {
                    if (col == 3) {
                        const bool ha = a.has && !a.shortHist, hb = b.has && !b.shortHist;
                        if (ha != hb) return ha;
                        if (!ha) return a.sym < b.sym;
                        return asc ? a.dev < b.dev : a.dev > b.dev;
                    }
                    if (col == 1) {
                        if (a.vol != b.vol) return asc ? a.vol < b.vol : a.vol > b.vol;
                        return a.sym < b.sym;
                    }
                    return asc ? a.sym < b.sym : a.sym > b.sym;
                });
            }
        }

        const int64_t now = nowMs();
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step()) {
            for (int ri = clip.DisplayStart; ri < clip.DisplayEnd; ++ri) {
                const ScrItem& r = rows[(size_t)ri];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                std::string disp = r.sym;
                if (disp.size() > 4 && disp.compare(disp.size() - 4, 4, "USDT") == 0) disp.resize(disp.size() - 4);
                const std::string label = disp + "##" + r.sym;
                if (ImGui::Selectable(label.c_str(), r.sym == symbol, ImGuiSelectableFlags_SpanAllColumns) &&
                    r.sym != symbol) {
                    symbol = r.sym;
                    std::snprintf(symBuf, sizeof symBuf, "%s", symbol.c_str());
                    startSession();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", r.sym.c_str());
                    if (r.has) ImGui::Text("updated %llds ago", (long long)((now - r.updMs) / 1000));
                    if (r.has && r.shortHist) ImGui::Text("history shorter than SMA %d", SMA_PERIOD);
                    if (!r.err.empty()) ImGui::Text("%s", r.err.c_str());
                    ImGui::EndTooltip();
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Remove")) toRemove = r.sym;
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(1);
                if (r.vol > 0) {
                    char vb[24];
                    fmtCompact(r.vol, vb, sizeof vb);
                    ImGui::TextUnformatted(vb);
                } else ImGui::TextDisabled("--");

                ImGui::TableSetColumnIndex(2);
                if (r.has) ImGui::Text("%.*f", r.prec, r.price);
                else       ImGui::TextDisabled("--");

                ImGui::TableSetColumnIndex(3);
                if (r.has && !r.shortHist) {
                    ImGui::PushStyleColor(ImGuiCol_Text, r.dev >= 0 ? COL_UP : COL_DN);
                    ImGui::Text("%+.2f%%", r.dev);
                    ImGui::PopStyleColor();
                } else if (r.has)          ImGui::TextDisabled("n/a");
                else if (!r.err.empty())   ImGui::TextDisabled("err");
                else                       ImGui::TextDisabled("...");
            }
        }
        ImGui::EndTable();
    }
    if (!toRemove.empty()) g_scr.remove(toRemove);

    ImGui::Unindent(5 * ui);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
