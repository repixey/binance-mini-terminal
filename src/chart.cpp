#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "config.h"
#include "net.h"
#include "state.h"
#include "ui.h"
#include "util.h"

static void priceTag(ImDrawList* dl, float ui, float x0, float x1, float y, const char* txt, ImU32 bg) {
    dl->AddRectFilled(ImVec2(x0, y - 8 * ui), ImVec2(x1, y + 8 * ui), bg);
    dl->AddText(ImVec2(x0 + 5 * ui, y - 6 * ui), IM_COL32(255, 255, 255, 255), txt);
}

void drawChart(const ImVec2& p0, const ImVec2& size) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ui = ImGui::GetFontSize() / 13.0f;
    const float AX_W = 76 * ui, AX_H = 20 * ui;
    const float cx0 = p0.x, cy0 = p0.y;
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_V, false)) showVol = !showVol;
    const float cx1 = p0.x + size.x - AX_W, cyAll = p0.y + size.y - AX_H;
    float volH = showVol ? std::max(48.0f * ui, (cyAll - cy0) * 0.2f) : 0.0f;
    if (cyAll - cy0 - volH < 120.0f * ui) volH = 0.0f;
    const float gap = volH > 0 ? 3.0f * ui : 0.0f;
    const float cy1 = cyAll - volH - gap;
    const float vy0 = cyAll - volH, vy1 = cyAll;
    const float cw = cx1 - cx0, ch = cy1 - cy0;

    ImGui::InvisibleButton("chart", size);
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    dl->AddRectFilled(p0, p0 + size, COL_BG);
    if (candles.empty() || cw < 60 || ch < 60) return;

    const int64_t iv = INTERVALS[ivIdx].ms;
    if (!std::isfinite(viewRightT) || !std::isfinite(barPx)) {
        barPx = 8.0;
        viewRightT = (double)candles.back().t + PAD_BARS * iv;
    }

    if (act && !measureDrag && ImGui::IsMouseDragging(0, 0.0f)) {
        viewRightT -= (double)io.MouseDelta.x / barPx * (double)iv;
        follow = false;
    }
    if (hov && io.MouseWheel != 0.0f) {
        const double nb = std::clamp(barPx * std::pow(1.15, (double)io.MouseWheel), 0.3, 80.0);
        const double ax = follow ? (double)cx1 : (double)io.MousePos.x;
        const double tA = viewRightT - (cx1 - ax) / barPx * (double)iv;
        barPx = nb;
        viewRightT = tA + (cx1 - ax) / barPx * (double)iv;
    }
    if (hov && ImGui::IsMouseDoubleClicked(0)) follow = true;

    const double maxRight = (double)candles.back().t + PAD_BARS * iv;
    const double minRight = (double)candles.front().t + 3.0 * iv;
    viewRightT = std::min(viewRightT, maxRight);
    viewRightT = std::max(viewRightT, std::min(minRight, maxRight));
    if (follow) viewRightT = maxRight;
    else if (viewRightT >= maxRight) follow = true;

    const double msPerPx = (double)iv / barPx;
    const double tLeft = viewRightT - cw * msPerPx;

    if (!reachedStart && !loadingOlder && candles.size() < MAX_CANDLES && glfwGetTime() > olderRetryAt &&
        (double)candles.front().t > tLeft - std::max((viewRightT - tLeft) * 0.5, (double)SMA_PERIOD * iv)) {
        loadingOlder = true;
        fetchKlines(g_gen.load(), symbol, INTERVALS[ivIdx].api, candles.front().t - 1, Kind::Older, HIST_LIMIT);
    }

    auto cmp = [](const Candle& c, double t) { return (double)c.t < t; };
    const size_t i0 = std::lower_bound(candles.begin(), candles.end(), tLeft - (double)iv, cmp) - candles.begin();
    const size_t i1 = std::lower_bound(candles.begin(), candles.end(), viewRightT + (double)iv, cmp) - candles.begin();
    if (i0 >= i1) return;

    double lo = candles[i0].l, hi = candles[i0].h;
    for (size_t i = i0; i < i1; ++i) {
        lo = std::min(lo, candles[i].l);
        hi = std::max(hi, candles[i].h);
    }
    double pad = (hi - lo) * 0.08;
    if (!(pad > 0)) pad = std::max(std::fabs(hi) * 0.001, 1e-9);
    const double pmin = lo - pad, pmax = hi + pad;
    auto Y = [&](double p) { return (float)(cy1 - (p - pmin) / (pmax - pmin) * ch); };

    {
        auto xToT = [&](float x) {
            const double t = viewRightT - (double)(cx1 - x) * msPerPx;
            return std::floor(t / (double)iv) * (double)iv + 0.5 * (double)iv;
        };
        auto yToP = [&](float y) { return pmin + (double)(cy1 - y) / ch * (pmax - pmin); };
        const bool inRect = io.MousePos.x >= cx0 && io.MousePos.x <= cx1 &&
                            io.MousePos.y >= cy0 && io.MousePos.y <= cy1;
        if (hov && inRect && ImGui::IsMouseClicked(0)) {
            if (io.KeyShift) {
                measureDrag = measureShown = true;
                mT1 = mT2 = xToT(io.MousePos.x);
                mP1 = mP2 = yToP(io.MousePos.y);
            } else {
                measureShown = false;
            }
        }
        if (measureDrag) {
            if (ImGui::IsMouseDown(0)) {
                mT2 = xToT(std::clamp(io.MousePos.x, cx0, cx1));
                mP2 = yToP(std::clamp(io.MousePos.y, cy0, cy1));
            } else measureDrag = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) measureDrag = measureShown = false;
    }

    const double step = niceStep((pmax - pmin) / (ch / (55.0 * ui)));
    const int dec = std::clamp((int)std::ceil(-std::log10(step) - 1e-9), 0, std::max(pricePrec, 0));
    const double k0 = std::ceil(pmin / step);

    static const int64_t TS[] = {60000LL, 300000LL, 900000LL, 1800000LL, 3600000LL, 10800000LL,
                                 21600000LL, 43200000LL, 86400000LL, 172800000LL, 604800000LL,
                                 2592000000LL, 7776000000LL, 31536000000LL};
    const int64_t minStep = (int64_t)(95.0 * ui * msPerPx);
    int64_t tstep = TS[sizeof(TS) / sizeof(TS[0]) - 1];
    for (int64_t s : TS) if (s >= minStep) { tstep = s; break; }
    const char* tfmt = tstep >= 2592000000LL ? "%m.%Y" : tstep >= 86400000LL ? "%d.%m" : "%H:%M";
    const int64_t t0 = (int64_t)(tLeft / (double)tstep) * tstep;

    dl->PushClipRect(ImVec2(cx0, cy0), ImVec2(cx1, cy1), true);
    for (int n = 0; n < 100; ++n) {
        const double pr = (k0 + n) * step;
        if (pr >= pmax) break;
        const float y = Y(pr);
        dl->AddLine(ImVec2(cx0, y), ImVec2(cx1, y), COL_GRID);
    }
    for (int n = 0; n < 64; ++n) {
        const int64_t t = t0 + n * tstep;
        if ((double)t > viewRightT) break;
        const float x = (float)(cx1 - (viewRightT - (double)t) / msPerPx);
        dl->AddLine(ImVec2(x, cy0), ImVec2(x, cy1), COL_GRID);
    }

    const float bw = std::max(1.0f, std::floor((float)barPx * 0.72f));
    for (size_t i = i0; i < i1; ++i) {
        const Candle& c = candles[i];
        const float xL = (float)(cx1 - (viewRightT - (double)c.t) / (double)iv * barPx);
        const float xm = std::floor(xL + (float)barPx * 0.5f) + 0.5f;
        const ImU32 col = c.c >= c.o ? COL_UP : COL_DN;
        dl->AddLine(ImVec2(xm, Y(c.h)), ImVec2(xm, Y(c.l)), col, 1.0f);
        if (barPx >= 3.0) {
            const float yo = Y(c.o), yc = Y(c.c);
            const float top = std::min(yo, yc);
            float bot = std::max(yo, yc);
            if (bot - top < 1.0f) bot = top + 1.0f;
            const float x0 = std::floor(xm - bw * 0.5f);
            dl->AddRectFilled(ImVec2(x0, top), ImVec2(x0 + bw, bot), col);
        }
    }

    {
        static std::vector<ImVec2> pts;
        pts.clear();
        const size_t N = (size_t)SMA_PERIOD;
        const size_t st = std::max(i0, N - 1);
        if (candles.size() >= N && st < i1) {
            double sum = 0;
            for (size_t k = st + 1 - N; k <= st; ++k) sum += candles[k].c;
            for (size_t i = st;;) {
                const float x = (float)(cx1 - (viewRightT - (double)candles[i].t) / (double)iv * barPx) + (float)barPx * 0.5f;
                pts.push_back(ImVec2(x, Y(sum / (double)N)));
                if (++i >= i1) break;
                sum += candles[i].c - candles[i - N].c;
            }
            if (pts.size() >= 2) dl->AddPolyline(pts.data(), (int)pts.size(), COL_SMA, ImDrawFlags_None, 1.5f * ui);
        }
        char lg[24];
        std::snprintf(lg, sizeof lg, "SMA %d", SMA_PERIOD);
        dl->AddText(ImVec2(cx0 + 8 * ui, cy0 + 6 * ui), COL_SMA, lg);
    }

    const Candle& last = candles.back();
    const float ly = Y(last.c);
    const ImU32 lcol = last.c >= last.o ? COL_UP : COL_DN;
    for (float x = cx0; x < cx1; x += 6.0f * ui) dl->AddLine(ImVec2(x, ly), ImVec2(x + 3.0f * ui, ly), lcol);

    if (measureShown) {
        const float x1 = (float)(cx1 - (viewRightT - mT1) / msPerPx);
        const float x2 = (float)(cx1 - (viewRightT - mT2) / msPerPx);
        const float y1 = Y(mP1), y2 = Y(mP2);
        const bool up = mP2 >= mP1;
        const ImU32 mc = up ? IM_COL32(41, 98, 255, 255) : IM_COL32(242, 54, 69, 255);
        const ImU32 mf = up ? IM_COL32(41, 98, 255, 45)  : IM_COL32(242, 54, 69, 45);
        const float rx0 = std::min(x1, x2), rx1 = std::max(x1, x2);
        const float ry0 = std::min(y1, y2), ry1 = std::max(y1, y2);
        const float mxc = (rx0 + rx1) * 0.5f, myc = (ry0 + ry1) * 0.5f;
        dl->AddRectFilled(ImVec2(rx0, ry0), ImVec2(rx1, ry1), mf);
        dl->AddRect(ImVec2(rx0, ry0), ImVec2(rx1, ry1), mc, 0.0f, 0, 1.0f);
        dl->AddLine(ImVec2(mxc, y1), ImVec2(mxc, y2), mc, 1.0f);
        dl->AddLine(ImVec2(x1, myc), ImVec2(x2, myc), mc, 1.0f);
        if (ry1 - ry0 > 12 * ui) {
            const float dy = y2 < y1 ? -1.0f : 1.0f;
            dl->AddTriangleFilled(ImVec2(mxc, y2), ImVec2(mxc - 4 * ui, y2 - dy * 7 * ui),
                                  ImVec2(mxc + 4 * ui, y2 - dy * 7 * ui), mc);
        }
        if (rx1 - rx0 > 12 * ui) {
            const float dx = x2 >= x1 ? 1.0f : -1.0f;
            dl->AddTriangleFilled(ImVec2(x2, myc), ImVec2(x2 - dx * 7 * ui, myc - 4 * ui),
                                  ImVec2(x2 - dx * 7 * ui, myc + 4 * ui), mc);
        }
        const double dP = mP2 - mP1;
        const double pct = mP1 != 0 ? dP / mP1 * 100.0 : 0.0;
        const double dT = std::fabs(mT2 - mT1);
        char l1[96], l2[96], dur[48];
        fmtDuration((int64_t)dT, dur, sizeof dur);
        std::snprintf(l1, sizeof l1, "%+.*f (%+.2f%%)", pricePrec, dP, pct);
        std::snprintf(l2, sizeof l2, "%lld bars, %s", (long long)std::llround(dT / (double)iv), dur);
        const ImVec2 s1 = ImGui::CalcTextSize(l1), s2 = ImGui::CalcTextSize(l2);
        const float pd = 6 * ui;
        const float bwid = std::max(s1.x, s2.x) + 2 * pd;
        const float bhei = s1.y + s2.y + 2 * pd + 2 * ui;
        float bx = mxc - bwid * 0.5f;
        float by = up ? ry0 - bhei - 6 * ui : ry1 + 6 * ui;
        bx = std::clamp(bx, cx0 + 2.0f, std::max(cx0 + 2.0f, cx1 - bwid - 2.0f));
        by = std::clamp(by, cy0 + 2.0f, std::max(cy0 + 2.0f, cy1 - bhei - 2.0f));
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bwid, by + bhei), mc, 2.0f * ui);
        dl->AddText(ImVec2(bx + (bwid - s1.x) * 0.5f, by + pd), IM_COL32(255, 255, 255, 255), l1);
        dl->AddText(ImVec2(bx + (bwid - s2.x) * 0.5f, by + pd + s1.y + 2 * ui), IM_COL32(255, 255, 255, 255), l2);
    }

    const bool inChart = hov && io.MousePos.x >= cx0 && io.MousePos.x <= cx1 &&
                         io.MousePos.y >= cy0 && io.MousePos.y <= cy1;
    const bool inAny = hov && io.MousePos.x >= cx0 && io.MousePos.x <= cx1 &&
                       io.MousePos.y >= cy0 && io.MousePos.y <= cyAll;
    const ImU32 cc = IM_COL32(120, 126, 140, 160);
    if (inAny) dl->AddLine(ImVec2(io.MousePos.x, cy0), ImVec2(io.MousePos.x, cy1), cc);
    if (inChart) dl->AddLine(ImVec2(cx0, io.MousePos.y), ImVec2(cx1, io.MousePos.y), cc);
    dl->PopClipRect();

    double vmScale = 1, vmaxShown = 0, lastAvgV = 0;
    bool haveAvg = false;
    {
        const size_t N = (size_t)SMA_PERIOD, cnt = i1 - i0;
        static std::vector<double> avgV, avgS;
        static std::vector<size_t> wsz;
        avgV.assign(cnt, 0.0); avgS.assign(cnt, 0.0); wsz.assign(cnt, 0);
        const double lastT = lastTradeMs;
        auto speedOf = [&](size_t k, double& sp) -> bool {
            const Candle& c = candles[k];
            if (!(c.o > 0)) return false;
            double mins = (double)iv / 60000.0;
            if (k + 1 == candles.size()) {
                const double el = lastT - (double)c.t;
                if (el < 5000.0) return false;
                mins = std::min(el, (double)iv) / 60000.0;
            }
            sp = (c.h - c.l) / c.o * 100.0 / mins;
            return true;
        };
        size_t lo = i0 >= N - 1 ? i0 - (N - 1) : 0;
        double sumV = 0, sumS = 0;
        size_t cntS = 0;
        auto addK = [&](size_t k) { sumV += candles[k].qv; double sp; if (speedOf(k, sp)) { sumS += sp; ++cntS; } };
        auto remK = [&](size_t k) { sumV -= candles[k].qv; double sp; if (speedOf(k, sp)) { sumS -= sp; --cntS; } };
        for (size_t k = lo; k < i0; ++k) addK(k);
        double vmax = 0;
        for (size_t i = i0; i < i1; ++i) {
            addK(i);
            if (i - lo + 1 > N) { remK(lo); ++lo; }
            avgV[i - i0] = sumV / (double)(i - lo + 1);
            avgS[i - i0] = cntS ? sumS / (double)cntS : 0.0;
            wsz[i - i0] = i - lo + 1;
            vmax = std::max(vmax, candles[i].qv);
            if (wsz[i - i0] >= 10) vmax = std::max(vmax, avgV[i - i0]);
        }
        if (wsz[cnt - 1] >= 10) { haveAvg = true; lastAvgV = avgV[cnt - 1]; }

        if (volH > 0) {
            vmaxShown = vmax;
            vmScale = (vmax > 0 ? vmax : 1.0) * 1.08;
            const float vh = vy1 - vy0;
            auto mixc = [](int r, int g, int b, float k) {
                return IM_COL32((int)(10 + (r - 10) * k), (int)(12 + (g - 12) * k), (int)(16 + (b - 16) * k), 255);
            };
            dl->PushClipRect(ImVec2(cx0, vy0), ImVec2(cx1, vy1), true);
            for (int n = 0; n < 64; ++n) {
                const int64_t t = t0 + n * tstep;
                if ((double)t > viewRightT) break;
                const float x = (float)(cx1 - (viewRightT - (double)t) / msPerPx);
                dl->AddLine(ImVec2(x, vy0), ImVec2(x, vy1), COL_GRID);
            }
            for (size_t i = i0; i < i1; ++i) {
                const Candle& c = candles[i];
                const float xL = (float)(cx1 - (viewRightT - (double)c.t) / (double)iv * barPx);
                const float xm = std::floor(xL + (float)barPx * 0.5f) + 0.5f;
                float br = 0.40f;
                double sp = 0;
                const double as = avgS[i - i0];
                if (speedOf(i, sp) && as > 0) br = 0.25f + 0.75f * (float)std::clamp(sp / as / 3.0, 0.0, 1.0);
                const ImU32 colB = mixc(0, 190, 90, br), colS = mixc(230, 50, 55, br);
                const double qbv = std::clamp(c.qb, 0.0, c.qv);
                float hTot = (float)(c.qv / vmScale) * vh;
                if (c.qv > 0 && hTot < 1.0f) hTot = 1.0f;
                const float hBuy = c.qv > 0 ? (float)(qbv / c.qv) * hTot : 0.0f;
                const float yb = vy1 - hBuy, yt = vy1 - hTot;
                if (barPx >= 3.0) {
                    const float x0 = std::floor(xm - bw * 0.5f);
                    dl->AddRectFilled(ImVec2(x0, yb), ImVec2(x0 + bw, vy1), colB);
                    dl->AddRectFilled(ImVec2(x0, yt), ImVec2(x0 + bw, yb), colS);
                } else {
                    dl->AddLine(ImVec2(xm, yb), ImVec2(xm, vy1), colB);
                    dl->AddLine(ImVec2(xm, yt), ImVec2(xm, yb), colS);
                }
            }
            static std::vector<ImVec2> ap;
            ap.clear();
            for (size_t i = i0; i < i1; ++i) {
                if (wsz[i - i0] < 10) continue;
                const float x = (float)(cx1 - (viewRightT - (double)candles[i].t) / (double)iv * barPx) + (float)barPx * 0.5f;
                ap.push_back(ImVec2(x, vy1 - (float)(avgV[i - i0] / vmScale) * vh));
            }
            if (ap.size() >= 2)
                dl->AddPolyline(ap.data(), (int)ap.size(), IM_COL32(230, 230, 240, 170), ImDrawFlags_None, 1.2f * ui);
            if (inAny) dl->AddLine(ImVec2(io.MousePos.x, vy0), ImVec2(io.MousePos.x, vy1), cc);
            dl->AddText(ImVec2(cx0 + 8 * ui, vy0 + 3 * ui), COL_TXT, "Vol USDT  (buy / sell)");
            dl->PopClipRect();
        }

        size_t li = i1 - 1;
        if (hov && io.MousePos.x >= cx0 && io.MousePos.x <= cx1) {
            const double tm = viewRightT - (double)(cx1 - io.MousePos.x) * msPerPx;
            const double bt = std::floor(tm / (double)iv) * (double)iv;
            const size_t j = std::lower_bound(candles.begin() + (long)i0, candles.begin() + (long)i1, bt, cmp) - candles.begin();
            if (j < i1 && (double)candles[j].t == bt) li = j;
        }
        const Candle& c = candles[li];
        char tb[32], l1[200], l2[240], vs[24], sps[64];
        fmtTime(c.t, "%d.%m %H:%M", tb, sizeof tb);
        const double chg = c.o != 0 ? (c.c - c.o) / c.o * 100.0 : 0.0;
        std::snprintf(l1, sizeof l1, "%s  O %.*f  H %.*f  L %.*f  C %.*f  %+.2f%%", tb,
                      pricePrec, c.o, pricePrec, c.h, pricePrec, c.l, pricePrec, c.c, chg);
        double sp = 0;
        const double as = avgS[li - i0], av = avgV[li - i0];
        if (speedOf(li, sp)) {
            if (sp < 0.01) std::snprintf(sps, sizeof sps, "%.4f%%/min (x%.1f)", sp, as > 0 ? sp / as : 0.0);
            else           std::snprintf(sps, sizeof sps, "%.3f%%/min (x%.1f)", sp, as > 0 ? sp / as : 0.0);
        } else std::snprintf(sps, sizeof sps, "--");
        fmtCompact(c.qv, vs, sizeof vs);
        const double buyPct = c.qv > 0 ? std::clamp(c.qb / c.qv, 0.0, 1.0) * 100.0 : 0.0;
        std::snprintf(l2, sizeof l2, "Vol %s  buy %.0f%% / sell %.0f%%  x%.1f avg  speed %s  trades %lld",
                      vs, buyPct, 100.0 - buyPct, av > 0 ? c.qv / av : 0.0, sps, (long long)c.n);
        const ImVec2 z1 = ImGui::CalcTextSize(l1), z2 = ImGui::CalcTextSize(l2);
        const float lh = ImGui::GetTextLineHeight() + 2 * ui;
        const float lx = cx0 + 8 * ui, ly0 = cy0 + 6 * ui + lh;
        dl->AddRectFilled(ImVec2(lx - 3 * ui, ly0 - 1 * ui), ImVec2(lx + std::max(z1.x, z2.x) + 3 * ui, ly0 + 2 * lh),
                          IM_COL32(10, 12, 16, 170));
        dl->AddText(ImVec2(lx, ly0), COL_TXT, l1);
        dl->AddText(ImVec2(lx, ly0 + lh), IM_COL32(215, 220, 230, 255), l2);
    }

    dl->AddRectFilled(ImVec2(cx1, cy0), ImVec2(p0.x + size.x, p0.y + size.y), COL_AXIS);
    dl->AddRectFilled(ImVec2(cx0, cyAll), ImVec2(cx1, p0.y + size.y), COL_AXIS);
    dl->AddLine(ImVec2(cx1, cy0), ImVec2(cx1, p0.y + size.y), COL_GRID);
    dl->AddLine(ImVec2(cx0, cyAll), ImVec2(p0.x + size.x, cyAll), COL_GRID);
    if (volH > 0) dl->AddLine(ImVec2(cx0, cy1 + gap * 0.5f), ImVec2(p0.x + size.x, cy1 + gap * 0.5f), COL_GRID);

    char buf[48];
    for (int n = 0; n < 100; ++n) {
        const double pr = (k0 + n) * step;
        if (pr >= pmax) break;
        std::snprintf(buf, sizeof buf, "%.*f", dec, pr);
        dl->AddText(ImVec2(cx1 + 6 * ui, Y(pr) - 6 * ui), COL_TXT, buf);
    }
    for (int n = 0; n < 64; ++n) {
        const int64_t t = t0 + n * tstep;
        if ((double)t > viewRightT) break;
        if ((double)t < tLeft) continue;
        const float x = (float)(cx1 - (viewRightT - (double)t) / msPerPx);
        fmtTime(t, tfmt, buf, sizeof buf);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        if (x - ts.x * 0.5f > cx0 && x + ts.x * 0.5f < cx1)
            dl->AddText(ImVec2(x - ts.x * 0.5f, cyAll + 4 * ui), COL_TXT, buf);
    }
    if (ly > cy0 && ly < cy1) {
        std::snprintf(buf, sizeof buf, "%.*f", pricePrec, last.c);
        priceTag(dl, ui, cx1, p0.x + size.x, ly, buf, lcol);
        const double pc = last.o != 0 ? (last.c - last.o) / last.o * 100.0 : 0.0;
        std::snprintf(buf, sizeof buf, "%+.2f%%", pc);
        float py = ly + 16 * ui;
        if (py + 8 * ui > cy1) py = ly - 16 * ui;
        priceTag(dl, ui, cx1, p0.x + size.x, py, buf, lcol);
    }
    if (candles.size() >= (size_t)SMA_PERIOD) {
        double sum = 0;
        for (size_t k = candles.size() - SMA_PERIOD; k < candles.size(); ++k) sum += candles[k].c;
        const double sv = sum / SMA_PERIOD;
        const float sy = Y(sv);
        const double dv = sv != 0 ? (last.c - sv) / sv * 100.0 : 0.0;
        if (sy > cy0 && sy < cy1 && std::fabs(sy - ly) > 50 * ui) {
            std::snprintf(buf, sizeof buf, "%.*f", pricePrec, sv);
            priceTag(dl, ui, cx1, p0.x + size.x, sy, buf, IM_COL32(150, 95, 0, 255));
            std::snprintf(buf, sizeof buf, "%+.2f%%", dv);
            float sy2 = sy + 16 * ui;
            if (sy2 + 8 * ui > cy1) sy2 = sy - 16 * ui;
            priceTag(dl, ui, cx1, p0.x + size.x, sy2, buf, dv >= 0 ? COL_UP : COL_DN);
        }
    }
    if (volH > 0 && vmaxShown > 0) {
        char vb[24];
        fmtCompact(vmaxShown, vb, sizeof vb);
        dl->AddText(ImVec2(cx1 + 6 * ui, vy0 + 3 * ui), COL_TXT, vb);
        if (haveAvg) {
            const float ay = vy1 - (float)(lastAvgV / vmScale) * (vy1 - vy0);
            if (ay > vy0 + 18 * ui && ay < vy1 - 6 * ui) {
                fmtCompact(lastAvgV, vb, sizeof vb);
                priceTag(dl, ui, cx1, p0.x + size.x, ay, vb, IM_COL32(90, 95, 110, 255));
            }
        }
    }
    if (inChart) {
        const double pr = pmin + (cy1 - io.MousePos.y) / ch * (pmax - pmin);
        std::snprintf(buf, sizeof buf, "%.*f", pricePrec, pr);
        priceTag(dl, ui, cx1, p0.x + size.x, io.MousePos.y, buf, IM_COL32(60, 66, 80, 255));
    }
}
