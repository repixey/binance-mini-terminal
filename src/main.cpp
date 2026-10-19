#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <ixwebsocket/IXNetSystem.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net.h"
#include "screener.h"
#include "session.h"
#include "state.h"
#include "ui.h"
#include "util.h"

struct GlProfile {
    int major, minor;
    bool core;
    const char* glsl;
};

static GLFWwindow* createWindow(int platform, const char*& glslOut) {
    glfwInitHint(GLFW_PLATFORM, platform);
    if (!glfwInit()) return nullptr;
    static const GlProfile profiles[] = {{3, 3, true, "#version 330"}, {3, 0, false, "#version 130"}};
    for (const auto& p : profiles) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, p.major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, p.minor);
        if (p.core) glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        glfwWindowHintString(GLFW_WAYLAND_APP_ID, "mini-terminal");
        glfwWindowHintString(GLFW_X11_CLASS_NAME, "mini-terminal");
        glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "mini-terminal");
        GLFWwindow* w = glfwCreateWindow(1280, 720, "mini terminal", nullptr, nullptr);
        if (w) { glslOut = p.glsl; return w; }
    }
    glfwTerminate();
    return nullptr;
}

static void setupFontsAndScale(GLFWwindow* win) {
    float xs = 1, ys = 1;
    glfwGetWindowContentScale(win, &xs, &ys);
    float cs = std::max(xs, ys);
    if (!(cs > 0.5f) || cs > 8.0f) cs = 1.0f;
    int ww = 0, wh = 0, fw = 0, fh = 0;
    glfwGetWindowSize(win, &ww, &wh);
    glfwGetFramebufferSize(win, &fw, &fh);
    const float fbs = ww > 0 ? (float)fw / (float)ww : 1.0f;

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    if (fbs > 1.01f) {
        cfg.SizePixels = 13.0f * fbs;
        io.Fonts->AddFontDefault(&cfg);
        io.FontGlobalScale = 1.0f / fbs;
    } else {
        cfg.SizePixels = 13.0f * cs;
        io.Fonts->AddFontDefault(&cfg);
        ImGui::GetStyle().ScaleAllSizes(cs);
    }
}

static void onSignal(int s) { g_sig = s; }

static int run(int argc, char** argv) {
    bool vsync = false;
    int forcedPlatform = GLFW_ANY_PLATFORM;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--vsync") vsync = true;
        else if (a == "--x11") forcedPlatform = GLFW_PLATFORM_X11;
        else if (a == "--wayland") forcedPlatform = GLFW_PLATFORM_WAYLAND;
        else {
            bool isIv = false;
            for (int k = 0; k < N_INTERVALS; ++k)
                if (a == INTERVALS[k].label || a == INTERVALS[k].api) { ivIdx = k; isIv = true; }
            if (!isIv) {
                std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return (char)std::toupper(c); });
                if (validSymbol(a)) symbol = a;
            }
        }
    }

    glfwSetErrorCallback([](int code, const char* desc) { std::fprintf(stderr, "GLFW error %d: %s\n", code, desc); });

    std::vector<int> platforms;
    if (forcedPlatform != GLFW_ANY_PLATFORM) platforms = {forcedPlatform};
    else platforms = {GLFW_ANY_PLATFORM, GLFW_PLATFORM_WAYLAND, GLFW_PLATFORM_X11};
    GLFWwindow* win = nullptr;
    const char* glsl = "#version 330";
    for (int p : platforms) {
        if (p != GLFW_ANY_PLATFORM && !glfwPlatformSupported(p)) continue;
        win = createWindow(p, glsl);
        if (win) break;
    }
    if (!win) { std::fprintf(stderr, "Не удалось создать окно/контекст OpenGL\n"); return 1; }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(vsync ? 1 : 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsClassic();
    ImGui::GetStyle().WindowRounding = 0;
    ImGui::GetStyle().FrameRounding = 0;
    setupFontsAndScale(win);
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl);

    g_http.start();
    g_wsw.start();
    std::snprintf(symBuf, sizeof symBuf, "%s", symbol.c_str());
    g_scr.start();
    startSession();

    const double minFrame = vsync ? 0.0 : 1.0 / 240.0;
    double lastRender = 0, lastTitle = 0;
    std::string lastPriceStr;

    while (!glfwWindowShouldClose(win) && !g_sig) {
        glfwWaitEventsTimeout(0.05);
        if (minFrame > 0) {
            const double dt = glfwGetTime() - lastRender;
            if (dt < minFrame) std::this_thread::sleep_for(std::chrono::duration<double>(minFrame - dt));
        }
        g_wake = false;
        try { drain(); housekeeping(); } catch (...) { setStatus("internal error"); }

        int fw = 0, fh = 0;
        glfwGetFramebufferSize(win, &fw, &fh);
        if (glfwGetWindowAttrib(win, GLFW_ICONIFIED) || fw <= 0 || fh <= 0) {
            glfwWaitEventsTimeout(0.25);
            continue;
        }
        lastRender = glfwGetTime();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, COL_BG);
        ImGui::Begin("##root", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNav);

        ImGui::SetCursorPos(ImVec2(6, 4));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.5f);
        if (ImGui::InputText("##sym", symBuf, sizeof symBuf,
                             ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank |
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            symbol = symBuf;
            startSession();
        }
        for (int i = 0; i < N_INTERVALS; ++i) {
            ImGui::SameLine(0, 3);
            const bool active = i == ivIdx;
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.80f, 1.0f));
            if (ImGui::Button(INTERVALS[i].label) && !active) {
                ivIdx = i;
                startSession();
            }
            if (active) ImGui::PopStyleColor();
        }
        std::string st;
        { std::lock_guard<std::mutex> lk(S.m); st = S.status; }
        if (!st.empty()) {
            ImGui::SameLine(0, 12);
            ImGui::TextUnformatted(st.c_str());
        }

        ImGui::SetCursorPos(ImVec2(0, ImGui::GetCursorPosY() + 4));
        {
            const float ui = ImGui::GetFontSize() / 13.0f;
            const ImVec2 full = ImGui::GetContentRegionAvail();
            const float panelW = (g_scrOpen ? 310.0f : 24.0f) * ui;
            if (full.x > panelW + 120.0f * ui && full.y > 10) {
                const ImVec2 org = ImGui::GetCursorPos();
                drawScreener(ImVec2(panelW, full.y));
                ImGui::SetCursorPos(ImVec2(org.x + panelW, org.y));
            }
        }
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x > 10 && avail.y > 10) drawChart(p0, avail);

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::Render();
        glViewport(0, 0, fw, fh);
        glClearColor(0.04f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);

        if (!candles.empty() && lastRender - lastTitle > 0.5) {
            lastTitle = lastRender;
            char price[48], title[128];
            const Candle& lc = candles.back();
            const double pc = lc.o != 0 ? (lc.c - lc.o) / lc.o * 100.0 : 0.0;
            std::snprintf(price, sizeof price, "%.*f %+.2f%%", pricePrec, lc.c, pc);
            if (lastPriceStr != price) {
                lastPriceStr = price;
                std::snprintf(title, sizeof title, "%s %s  %s", symbol.c_str(), price, INTERVALS[ivIdx].label);
                glfwSetWindowTitle(win, title);
            }
        }
    }

    g_quit = true;
    ++g_gen;
    cancelHttp();
    g_wsw.post([] { if (g_ws) { g_ws->stop(); g_ws.reset(); } });
    g_scr.stop();
    g_wsw.stop();
    g_http.stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}

int main(int argc, char** argv) {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGCHLD, SIG_IGN);
#endif
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    ix::initNetSystem();
    int rc = 1;
    try {
        rc = run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "fatal: unknown exception\n");
    }
    ix::uninitNetSystem();
    return rc;
}
