// MouseTrailFx v0.1 �?桌面级「点击效果与拖尾效果�?
// 逆向�?dsh-plugin-wallpaper-engine-fx v1.4.0 的绘制数学，底座换成 Win32 分层窗口 + 软件光栅�?
// 构建: g++ -std=c++20 -O2 -municode -mwindows -static src/main.cpp -o MouseTrailFx.exe -lgdi32 -luser32 -lwinmm -lshell32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <map>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ───────────────────────────── 设置�?4 项，与插�?settings-schema 对齐�?─────────────────────────────
struct Settings {
    bool  on         = true;
    bool  clickOn    = true;
    int   clickStyle = 0;      // 0=涟漪 1=星火 2=两�?
    float clickSize  = 50.f;   // 30..400
    float clickGlow  = 45.f;   // 0..100
    bool  trailOn    = true;
    int   trailStyle = 0;      // 0=彗尾 1=星尘
    float trailLife  = 420.f;  // 80..2000 ms
    float trailWidth = 3.f;    // 1..12 px
    float trailGlow  = 60.f;   // 0..100
    float opacity    = 85.f;   // 10..100
    int   blend      = 0;      // 0=screen 1=normal 2=overlay 3=multiply 4=lighten
    int   colorMode  = 0;      // 0=主题�?1=彩虹 2=自定�?
    unsigned color   = 0x4f8cff;
    bool  autostart  = false;
    float hue        = 0.f;
};
static Settings g_s;
static bool g_captureHidden = true;   // true = 截图/录屏看不到这层

static inline float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void LogF(const char* fmt, ...);

// ───────────────────────────── 迷你 JSON（够用即可：对象/数组/数字/字符�?布尔�?─────────────────────────────
struct JVal {
    enum T { NUL, BOOL, NUM, STR, OBJ, ARR } t = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<std::pair<std::string, JVal>> obj;
    std::vector<JVal> arr;
    const JVal* get(const char* k) const {
        if (t != OBJ) return nullptr;
        for (auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
};

struct JParser {
    const char* p;
    const char* e;
    bool ok = true;
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    bool lit(const char* s) {
        size_t n = strlen(s);
        if (size_t(e - p) < n || strncmp(p, s, n) != 0) return false;
        p += n; return true;
    }
    std::string str() {
        std::string r;
        if (p >= e || *p != '"') { ok = false; return r; }
        ++p;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                ++p;
                char c = *p++;
                r += (c == 'n') ? '\n' : (c == 't') ? '\t' : c;
            } else r += *p++;
        }
        if (p < e) ++p;
        return r;
    }
    JVal value() {
        JVal v; ws();
        if (p >= e) { ok = false; return v; }
        char c = *p;
        if (c == '{') {
            v.t = JVal::OBJ; ++p; ws();
            if (p < e && *p == '}') { ++p; return v; }
            while (p < e && ok) {
                ws(); std::string k = str(); ws();
                if (p >= e || *p != ':') { ok = false; break; }
                ++p;
                v.obj.emplace_back(k, value());
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; break; }
                ok = false;
            }
            return v;
        }
        if (c == '[') {
            v.t = JVal::ARR; ++p; ws();
            if (p < e && *p == ']') { ++p; return v; }
            while (p < e && ok) {
                v.arr.push_back(value()); ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; break; }
                ok = false;
            }
            return v;
        }
        if (c == '"') { v.t = JVal::STR; v.s = str(); return v; }
        if (lit("true")) { v.t = JVal::BOOL; v.b = true; return v; }
        if (lit("false")) { v.t = JVal::BOOL; v.b = false; return v; }
        if (lit("null")) { return v; }
        char* end = nullptr;
        v.n = strtod(p, &end);
        if (end && end > p) { p = end; v.t = JVal::NUM; }
        else ok = false;
        return v;
    }
};

static std::string g_cfgPath;
static ULONGLONG   g_cfgStamp = 0;
static UINT        g_showMsg = 0;
static UINT        g_taskbarMsg = 0;

static void ConfigPathInit() {
    char buf[MAX_PATH] = {0};
    if (GetEnvironmentVariableA("APPDATA", buf, MAX_PATH) > 0) {
        snprintf(buf + strlen(buf), MAX_PATH - strlen(buf), "\\MouseTrailFx");
        CreateDirectoryA(buf, nullptr);
        snprintf(buf + strlen(buf), MAX_PATH - strlen(buf), "\\config.json");
        g_cfgPath = buf;
    } else {
        char exe[MAX_PATH];
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        char* slash = strrchr(exe, '\\');
        if (slash) *slash = 0;
        g_cfgPath = std::string(exe) + "\\config.json";
    }
}

static const char* BlendName(int b) {
    static const char* n[] = { "screen", "normal", "overlay", "multiply", "lighten" };
    return n[b < 0 || b > 4 ? 0 : b];
}
static int BlendFromName(const std::string& s) {
    if (s == "normal") return 1;
    if (s == "overlay") return 2;
    if (s == "multiply") return 3;
    if (s == "lighten") return 4;
    return 0;
}

static void LoadConfig() {
    FILE* f = fopen(g_cfgPath.c_str(), "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 1 << 20) { fclose(f); return; }
    std::string txt(size_t(n), '\0');
    fread(&txt[0], 1, size_t(n), f);
    fclose(f);
    if (txt.size() >= 3 && (unsigned char)txt[0] == 0xEF && (unsigned char)txt[1] == 0xBB &&
        (unsigned char)txt[2] == 0xBF)
        txt.erase(0, 3);   // 记事�?PowerShell 写入�?UTF-8 BOM

    JParser jp{ txt.c_str(), txt.c_str() + txt.size() };
    JVal root = jp.value();
    if (!jp.ok || root.t != JVal::OBJ) { LogF("config parse failed (%d bytes)", int(txt.size())); return; }

    auto num = [&](const JVal* o, const char* k, float& dst, float lo, float hi) {
        if (o) if (const JVal* v = o->get(k)) if (v->t == JVal::NUM) dst = ClampF(float(v->n), lo, hi);
    };
    auto boolean = [&](const JVal* o, const char* k, bool& dst) {
        if (o) if (const JVal* v = o->get(k)) if (v->t == JVal::BOOL) dst = v->b;
    };

    boolean(&root, "enabled", g_s.on);
    boolean(&root, "autostart", g_s.autostart);
    boolean(&root, "captureHidden", g_captureHidden);
    num(&root, "opacity", g_s.opacity, 10.f, 100.f);
    if (const JVal* b = root.get("blend")) if (b->t == JVal::STR) g_s.blend = BlendFromName(b->s);

    if (const JVal* c = root.get("click")) {
        boolean(c, "on", g_s.clickOn);
        num(c, "size", g_s.clickSize, 30.f, 400.f);
        num(c, "glow", g_s.clickGlow, 0.f, 100.f);
        if (const JVal* s = c->get("style")) if (s->t == JVal::STR)
            g_s.clickStyle = s->s == "spark" ? 1 : (s->s == "both" ? 2 : 0);
    }
    if (const JVal* t = root.get("trail")) {
        boolean(t, "on", g_s.trailOn);
        num(t, "length", g_s.trailLife, 80.f, 2000.f);
        num(t, "width", g_s.trailWidth, 1.f, 12.f);
        num(t, "glow", g_s.trailGlow, 0.f, 100.f);
        if (const JVal* s = t->get("style")) if (s->t == JVal::STR)
            g_s.trailStyle = (s->s == "dust") ? 1 : 0;
    }
    if (const JVal* c = root.get("color")) {
        if (const JVal* m = c->get("mode")) if (m->t == JVal::STR)
            g_s.colorMode = m->s == "rainbow" ? 1 : (m->s == "custom" ? 2 : 0);
        if (const JVal* v = c->get("value")) if (v->t == JVal::STR && v->s.size() == 7 && v->s[0] == '#') {
            unsigned rgb = (unsigned)strtoul(v->s.c_str() + 1, nullptr, 16);
            g_s.color = rgb & 0xFFFFFF;
        }
    }
    LogF("config loaded: %s", g_cfgPath.c_str());
}

static void SaveConfig() {
    FILE* f = fopen(g_cfgPath.c_str(), "wb");
    if (!f) { LogF("config save failed: %s", g_cfgPath.c_str()); return; }
    const char* clickStyle = g_s.clickStyle == 1 ? "spark" : (g_s.clickStyle == 2 ? "both" : "ripple");
    const char* trailStyle = g_s.trailStyle == 1 ? "dust" : "comet";
    const char* colorMode = g_s.colorMode == 1 ? "rainbow" : (g_s.colorMode == 2 ? "custom" : "accent");
    fprintf(f,
        "{\n"
        "  \"enabled\": %s,\n"
        "  \"autostart\": %s,\n"
        "  \"captureHidden\": %s,\n"
        "  \"opacity\": %.0f,\n"
        "  \"blend\": \"%s\",\n"
        "  \"click\": { \"on\": %s, \"style\": \"%s\", \"size\": %.0f, \"glow\": %.0f },\n"
        "  \"trail\": { \"on\": %s, \"style\": \"%s\", \"length\": %.0f, \"width\": %.1f, \"glow\": %.0f },\n"
        "  \"color\": { \"mode\": \"%s\", \"value\": \"#%06x\" }\n"
        "}\n",
        g_s.on ? "true" : "false", g_s.autostart ? "true" : "false",
        g_s.on ? "true" : "false", g_s.autostart ? "true" : "false", g_captureHidden ? "true" : "false",
        g_s.clickOn ? "true" : "false", clickStyle, g_s.clickSize, g_s.clickGlow,
        g_s.trailOn ? "true" : "false", trailStyle, g_s.trailLife, g_s.trailWidth, g_s.trailGlow,
        colorMode, g_s.color);
    fclose(f);
}

// 预设
static void ApplyPreset(int which) {
    g_s.clickSize = 50.f; g_s.clickGlow = 45.f; g_s.opacity = 85.f;
    g_s.trailLife = 420.f; g_s.trailWidth = 3.f; g_s.trailGlow = 60.f;
    g_s.blend = 0; g_s.colorMode = 0; g_s.trailStyle = 0; g_s.clickStyle = 0;
    switch (which) {
        case 1:  // 霓虹
            g_s.trailWidth = 4.f; g_s.trailGlow = 85.f; g_s.clickGlow = 80.f;
            g_s.clickSize = 80.f; g_s.opacity = 95.f; break;
        case 2:  // 极简
            g_s.trailWidth = 2.f; g_s.trailGlow = 20.f; g_s.clickGlow = 10.f;
            g_s.clickSize = 40.f; g_s.opacity = 65.f; g_s.trailLife = 260.f; break;
        case 3:  // 星尘
            g_s.trailStyle = 1; g_s.clickStyle = 1; g_s.trailGlow = 55.f;
            g_s.trailWidth = 2.f; break;
        default: break;
    }
    SaveConfig();
    LogF("preset %d", which);
}

// 开机自�?
static void ApplyAutostart(bool enable) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (enable) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring val = std::wstring(L"\"") + exe + L"\" /silent";
        RegSetValueExW(k, L"MouseTrailFx", 0, REG_SZ, (const BYTE*)val.c_str(),
                       (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"MouseTrailFx");
    }
    RegCloseKey(k);
}

static bool IsAutostartOn() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    bool on = RegQueryValueExW(k, L"MouseTrailFx", nullptr, &type, nullptr, &size) == ERROR_SUCCESS;
    RegCloseKey(k);
    return on;
}

// 快捷方式（桌�?+ 开始菜单）
static bool CreateShortcutAt(const std::wstring& lnk, const std::wstring& target) {
    IShellLinkW* sl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW, (void**)&sl);
    if (FAILED(hr)) { LogF("CoCreateInstance hr=0x%08lx", (unsigned long)hr); return false; }
    wchar_t dir[MAX_PATH];
    wcsncpy(dir, target.c_str(), MAX_PATH - 1);
    dir[MAX_PATH - 1] = 0;
    for (int i = (int)wcslen(dir) - 1; i >= 0; --i) {
        if (dir[i] == L'\\') { dir[i] = 0; break; }
    }
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(dir);
    sl->SetIconLocation(target.c_str(), 0);
    sl->SetDescription(L"桌面点击与拖尾效果");
    IPersistFile* pf = nullptr;
    bool ok = false;
    HRESULT hq = sl->QueryInterface(IID_IPersistFile, (void**)&pf);
    if (SUCCEEDED(hq) && pf) {
        HRESULT hs = pf->Save(lnk.c_str(), TRUE);
        LogF("shortcut save hr=0x%08lx '%ls'", (unsigned long)hs, lnk.c_str());
        ok = SUCCEEDED(hs);
        pf->Release();
    } else {
        LogF("shortcut qi hr=0x%08lx", (unsigned long)hq);
    }
    sl->Release();
    return ok;
}

static void MakeShortcuts() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);

    // 优先�?exe 旁边的脚本：IShellLink 在某些环境下 Save 会返�?ACCESS_DENIED
    wchar_t dir[MAX_PATH];
    wcsncpy(dir, exe, MAX_PATH - 1);
    dir[MAX_PATH - 1] = 0;
    for (int i = (int)wcslen(dir) - 1; i >= 0; --i) {
        if (dir[i] == L'\\') { dir[i] = 0; break; }
    }
    std::wstring ps1 = std::wstring(dir) + L"\\install-shortcut.ps1";
    if (GetFileAttributesW(ps1.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + ps1 + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 8000);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            LogF("shortcut via ps1 exit=%lu", code);
            return;
        }
        LogF("shortcut ps1 spawn failed err=%lu", GetLastError());
    }

    bool a = false, b = false;
    PWSTR p = nullptr;
    HRESULT h1 = SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &p);
    if (SUCCEEDED(h1) && p) {
        a = CreateShortcutAt(std::wstring(p) + L"\\MouseTrailFx.lnk", exe);
        CoTaskMemFree(p);
    }
    p = nullptr;
    HRESULT h2 = SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &p);
    if (SUCCEEDED(h2) && p) {
        b = CreateShortcutAt(std::wstring(p) + L"\\MouseTrailFx.lnk", exe);
        CoTaskMemFree(p);
    }
    LogF("shortcut desktop=%d programs=%d", int(a), int(b));
}

// 源实现常量（�?client.fx.js 一致）
static constexpr float FX_CLICK_LIFE_MS = 900.f;
static constexpr float FX_TRAIL_STEP_PX = 3.f;
static constexpr int   FX_TRAIL_MAX     = 2000;   // 源为 96（会截断快甩），这里放开
static constexpr int   FX_SPARK_COUNT   = 12;

// ───────────────────────────── 全局 ─────────────────────────────
static HWND     g_hwnd = nullptr;
static HDC      g_screenDC = nullptr, g_memDC = nullptr;
static HBITMAP  g_bmp = nullptr;
static uint32_t* g_bits = nullptr;
static int      g_W = 0, g_H = 0;          // 虚拟桌面尺寸
static int      g_VX = 0, g_VY = 0;        // 虚拟桌面原点
static LARGE_INTEGER g_freq{};
static LONGLONG g_t0 = 0;
static bool     g_shown = false;
static int      g_lastX = INT_MIN, g_lastY = INT_MIN;
static bool     g_lastDown = false;
static int      g_mode = 0;            // 0=正常 1=demo 2=只点�?3=只拖�?4=冻结展示
static double   g_freezeT = 0;
static HHOOK    g_hMouse = nullptr;
static volatile LONG g_hookX = 0, g_hookY = 0;
static volatile LONG g_hookEvents = 0, g_rawEvents = 0, g_hookClicks = 0;
static float    g_hue = 0.f;
static int      g_seed = 0;

static float g_accR = 0.13f, g_accG = 0.78f, g_accB = 0.84f;
static CRITICAL_SECTION g_lock;
static volatile LONG g_stopInput = 0;

struct Lock { Lock() { EnterCriticalSection(&g_lock); } ~Lock() { LeaveCriticalSection(&g_lock); } };

static void LogF(const char* fmt, ...) {
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    char full[MAX_PATH];
    snprintf(full, sizeof(full), "%s\\mtfx.log", exe);
    FILE* f = fopen(full, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

struct Particle { float x, y; double t; float hue; };
struct ClickFx  { float x, y; double t0; int seed; float hue; };
static std::vector<Particle> g_trail;
static std::vector<ClickFx>  g_clicks;

// ───────────────────────────── 时间 ─────────────────────────────
static inline double NowMs() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart - g_t0) * 1000.0 / double(g_freq.QuadPart);
}

// ───────────────────────────── 颜色 ─────────────────────────────
static void HsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float c = v * s;
    float hp = fmodf(h, 360.f); if (hp < 0) hp += 360.f; hp /= 60.f;
    float x = c * (1.f - fabsf(fmodf(hp, 2.f) - 1.f));
    float m = v - c;
    float rr = 0, gg = 0, bb = 0;
    int i = int(floorf(hp)) % 6;
    switch (i) {
        case 0: rr = c; gg = x; break;
        case 1: rr = x; gg = c; break;
        case 2: gg = c; bb = x; break;
        case 3: gg = x; bb = c; break;
        case 4: rr = x; bb = c; break;
        default: rr = c; bb = x; break;
    }
    r = rr + m; g = gg + m; b = bb + m;
}

// 与源一致：accent 用主题色；rainbow 每实例色相不同且随时间流转；custom 固定�?
static float BlendGain(int blend) {
    switch (blend) {
        case 1: return 1.00f;   // normal
        case 2: return 1.35f;   // overlay 近似
        case 3: return 0.65f;   // multiply 近似
        case 4: return 1.50f;   // lighten 近似
        default: return 1.30f;  // screen 近似
    }
}

static void ColorFor(float hue, double now, float& r, float& g, float& b) {
    if (g_s.colorMode == 2) {
        r = float((g_s.color >> 16) & 0xFF) / 255.f;
        g = float((g_s.color >> 8) & 0xFF) / 255.f;
        b = float(g_s.color & 0xFF) / 255.f;
    } else if (g_s.colorMode == 1) {
        float h = fmodf(hue + float(now) / 12.f, 360.f);
        HsvToRgb(h, 0.92f, 0.66f, r, g, b);
    } else {
        r = g_accR; g = g_accG; b = g_accB;
    }
    // 桌面没有 screen 混合，靠提亮 + 档位增益接近"霓虹"观感
    float k = BlendGain(g_s.blend);
    r = std::min(1.f, r * k + 0.05f);
    g = std::min(1.f, g * k + 0.05f);
    b = std::min(1.f, b * k + 0.05f);
}

// ───────────────────────────── 光栅 ─────────────────────────────
static inline int Clamp255(float v) {
    int i = int(v * 255.f + 0.5f);
    return i < 0 ? 0 : (i > 255 ? 255 : i);
}

// 预乘 source-over；颜色通道未预乘传入，内部�?alpha
static inline void BlendPx(int x, int y, float r, float g, float b, float a) {
    if (a <= 0.002f) return;
    if (x < 0 || y < 0 || x >= g_W || y >= g_H) return;
    uint32_t* p = g_bits + size_t(y) * g_W + x;
    uint32_t v = *p;
    const float k = 1.f / 255.f;
    float da = float((v >> 24) & 0xFF) * k;
    float db = float(v & 0xFF) * k;
    float dg = float((v >> 8) & 0xFF) * k;
    float dr = float((v >> 16) & 0xFF) * k;
    float inv = 1.f - a;
    float na = a + da * inv;
    float nr = r * a + dr * inv;
    float ng = g * a + dg * inv;
    float nb = b * a + db * inv;
    *p = uint32_t(Clamp255(nb)) | (uint32_t(Clamp255(ng)) << 8) |
         (uint32_t(Clamp255(nr)) << 16) | (uint32_t(Clamp255(na)) << 24);
}

static inline float DistSeg2(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float L2 = vx * vx + vy * vy;
    float t = (L2 > 1e-6f) ? (wx * vx + wy * vy) / L2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    float dx = wx - vx * t, dy = wy - vy * t;
    return dx * dx + dy * dy;
}

static void DrawCapsule(float ax, float ay, float bx, float by, float rad,
                        float r, float g, float b, float a) {
    if (a <= 0.002f) return;
    int x0 = int(floorf(std::min(ax, bx) - rad - 1)), x1 = int(ceilf(std::max(ax, bx) + rad + 1));
    int y0 = int(floorf(std::min(ay, by) - rad - 1)), y1 = int(ceilf(std::max(ay, by) + rad + 1));
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, g_W - 1); y1 = std::min(y1, g_H - 1);
    const float outer = rad + 0.5f, inner = std::max(0.f, rad - 0.5f);
    const float o2 = outer * outer, i2 = inner * inner;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float d2 = DistSeg2(x + 0.5f, y + 0.5f, ax, ay, bx, by);
            if (d2 >= o2) continue;
            float cov = (d2 <= i2) ? 1.f : (outer - sqrtf(d2));
            BlendPx(x, y, r, g, b, a * cov);
        }
    }
}

// 圆环：|d - R| < thick/2
static void DrawRing(float cx, float cy, float R, float thick,
                     float r, float g, float b, float a) {
    if (a <= 0.002f || R <= 0.f) return;
    float half = std::max(0.35f, thick * 0.5f);
    int x0 = int(floorf(cx - R - half - 1)), x1 = int(ceilf(cx + R + half + 1));
    int y0 = int(floorf(cy - R - half - 1)), y1 = int(ceilf(cy + R + half + 1));
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, g_W - 1); y1 = std::min(y1, g_H - 1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = std::clamp(half + 0.5f - fabsf(d - R), 0.f, 1.f);
            if (cov > 0.f) BlendPx(x, y, r, g, b, a * cov);
        }
    }
}

static void DrawDisc(float cx, float cy, float R, float r, float g, float b, float a) {
    if (a <= 0.002f || R <= 0.f) return;
    int x0 = int(floorf(cx - R - 1)), x1 = int(ceilf(cx + R + 1));
    int y0 = int(floorf(cy - R - 1)), y1 = int(ceilf(cy + R + 1));
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, g_W - 1); y1 = std::min(y1, g_H - 1);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cov = std::clamp(R + 0.5f - sqrtf(dx * dx + dy * dy), 0.f, 1.f);
            if (cov > 0.f) BlendPx(x, y, r, g, b, a * cov);
        }
    }
}

// 光晕近似：多遍低 alpha 放大描边（代�?shadowBlur 的真高斯�?
static void GlowCapsule(float ax, float ay, float bx, float by, float rad,
                        float r, float g, float b, float a, float glowPct) {
    if (glowPct <= 0.f) return;
    float k = glowPct / 100.f;
    DrawCapsule(ax, ay, bx, by, rad * 4.0f + 2.0f, r, g, b, a * 0.30f * k);
    DrawCapsule(ax, ay, bx, by, rad * 1.9f + 0.6f, r, g, b, a * 0.45f * k);
}

static void GlowRing(float cx, float cy, float R, float thick,
                     float r, float g, float b, float a, float glowPct) {
    if (glowPct <= 0.f) return;
    float k = glowPct / 100.f;
    DrawRing(cx, cy, R, thick * 5.0f, r, g, b, a * 0.14f * k);
    DrawRing(cx, cy, R, thick * 2.4f, r, g, b, a * 0.24f * k);
}

// ───────────────────────────── 模型 ─────────────────────────────
static void PushTrail(float x, float y, double now) {
    if (g_trail.empty()) {
        g_trail.push_back({x, y, now, g_hue});
        g_hue = fmodf(g_hue + 24.f, 360.f);
        return;
    }
    const Particle& last = g_trail.back();
    float dx = x - last.x, dy = y - last.y;
    float d = sqrtf(dx * dx + dy * dy);
    if (d < FX_TRAIL_STEP_PX) return;
    int n = int(d / FX_TRAIL_STEP_PX);
    n = std::min(n, 64);
    for (int i = 1; i <= n; ++i) {
        float t = float(i) / float(n);
        g_trail.push_back({last.x + dx * t, last.y + dy * t, now, g_hue});
        g_hue = fmodf(g_hue + 24.f, 360.f);
    }
    if (int(g_trail.size()) > FX_TRAIL_MAX)
        g_trail.erase(g_trail.begin(), g_trail.begin() + (g_trail.size() - FX_TRAIL_MAX));
}

static void SpawnClick(float x, float y, double now) {
    g_seed += 1;
    g_clicks.push_back({x, y, now, g_seed, g_hue});
    if (g_clicks.size() > 24) g_clicks.erase(g_clicks.begin(), g_clicks.begin() + (g_clicks.size() - 24));
}

// 两个来源（低级钩�?/ 轮询）都会报点击，去重避免一次点击炸两圈
static double g_lastClickAt = -1000.0;
static bool TryClick(float x, float y, double now, const char* src) {
    if (now - g_lastClickAt < 60.0) return false;
    g_lastClickAt = now;
    SpawnClick(x, y, now);
    LogF("click[%s] %.0f,%.0f", src, x, y);
    return true;
}

// ───────────────────────────── 绘制 ─────────────────────────────
static void DrawTrail(double now, const std::vector<Particle>& trail) {
    if (!g_s.trailOn || trail.size() < 2) return;
    const float life = g_s.trailLife;
    const float op = g_s.opacity / 100.f;

    if (g_s.trailStyle == 1) {   // 星尘：每 10px 留一个亮�?
        float acc = 0.f;
        for (size_t i = 1; i < trail.size(); ++i) {
            const Particle& a = trail[i - 1];
            const Particle& b = trail[i];
            acc += fabsf(b.x - a.x) + fabsf(b.y - a.y);
            if (acc < 10.f) continue;
            acc = 0.f;
            float f = 1.f - float(now - b.t) / life;
            if (f <= 0.f) continue;
            float alpha = std::clamp(powf(f, 1.5f), 0.f, 1.f) * op;
            float rad = std::max(0.7f, g_s.trailWidth * (0.45f + 0.75f * f));
            float r, g, bl;
            ColorFor(b.hue, now, r, g, bl);
            if (g_s.trailGlow > 0.f) {
                float k = g_s.trailGlow / 100.f;
                DrawDisc(b.x, b.y, rad * 3.2f + 1.f, r, g, bl, alpha * 0.16f * k);
                DrawDisc(b.x, b.y, rad * 1.8f + 0.5f, r, g, bl, alpha * 0.32f * k);
            }
            DrawDisc(b.x, b.y, rad, r, g, bl, alpha);
        }
        return;
    }
    for (size_t i = 1; i < trail.size(); ++i) {
        const Particle& a = trail[i - 1];
        const Particle& b = trail[i];
        float f = 1.f - float(now - b.t) / life;
        if (f <= 0.f) continue;
        float alpha = std::clamp(powf(f, 1.5f), 0.f, 1.f) * op;
        float w = std::max(0.8f, g_s.trailWidth * (0.35f + 0.65f * f));
        float r, g, bl;
        ColorFor(b.hue, now, r, g, bl);
        GlowCapsule(a.x, a.y, b.x, b.y, w * 0.5f, r, g, bl, alpha, g_s.trailGlow);
        DrawCapsule(a.x, a.y, b.x, b.y, w * 0.5f, r, g, bl, alpha);
    }
}

static void DrawClicks(double now, const std::vector<ClickFx>& clicks) {
    if (!g_s.clickOn) return;
    const float op = g_s.opacity / 100.f;
    for (const ClickFx& c : clicks) {
        float p = float((now - c.t0) / FX_CLICK_LIFE_MS);
        if (p >= 1.f) continue;
        float r, g, bl;
        ColorFor(c.hue, now, r, g, bl);

        if (g_s.clickStyle != 1) {              // 涟漪（两圈，第二圈晚 18%�?
            for (int k = 0; k < 2; ++k) {
                float q = std::min(1.f, p - k * 0.18f);
                if (q <= 0.f) continue;
                float fade = (1.f - q) * (1.f - q);
                float R = g_s.clickSize * (1.f - powf(1.f - q, 3.f)) * (k ? 0.78f : 1.f);
                float thick = std::max(0.8f, 3.6f * (1.f - q));
                float a = 1.0f * fade * op;
                GlowRing(c.x, c.y, R, thick, r, g, bl, a, g_s.clickGlow);
                DrawRing(c.x, c.y, R, thick, r, g, bl, a);
            }
        }
        if (g_s.clickStyle != 0) {              // 星火
            float ease = 1.f - powf(1.f - p, 3.f);
            float fade = (1.f - p) * (1.f - p);
            float a = 0.9f * fade * op;
            float lw = std::max(0.6f, g_s.trailWidth * 0.7f * (1.f - p));
            for (int i = 0; i < FX_SPARK_COUNT; ++i) {
                float noise = float((c.seed * 9301 + i * 49297) % 233280) / 233280.f;
                float ang = (float(i) / FX_SPARK_COUNT) * float(M_PI) * 2.f + noise * 0.6f;
                float k = 0.45f + 0.55f * (float((c.seed * 31 + i * 37) % 100) / 100.f);
                float reach = g_s.clickSize * k * ease;
                float cs = cosf(ang), sn = sinf(ang);
                float ax = c.x + cs * reach * 0.7f, ay = c.y + sn * reach * 0.7f;
                float bx = c.x + cs * reach,        by = c.y + sn * reach;
                GlowCapsule(ax, ay, bx, by, lw * 0.5f, r, g, bl, a, g_s.clickGlow);
                DrawCapsule(ax, ay, bx, by, lw * 0.5f, r, g, bl, a);
            }
            float flash = std::max(0.f, 1.f - p * 3.f);
            if (flash > 0.f) {
                DrawDisc(c.x, c.y, std::max(0.5f, g_s.trailWidth * 1.6f * flash + 1.f),
                         1.f, 1.f, 1.f, 0.75f * flash * op);
            }
        }
    }
}

// ───────────────────────────── �?─────────────────────────────
static void HideOverlay() {
    if (g_shown) {
        ShowWindow(g_hwnd, SW_HIDE);
        g_shown = false;
    }
}

static void EnsureOverlaySane();
static void Render() {
    EnsureOverlaySane();
    double now = g_mode == 4 ? g_freezeT : NowMs();
    const float life = g_s.trailLife;

    std::vector<Particle> trail;
    std::vector<ClickFx> clicks;
    {
        Lock lk;
        g_trail.erase(std::remove_if(g_trail.begin(), g_trail.end(),
                      [&](const Particle& p) { return now - p.t > life; }), g_trail.end());
        g_clicks.erase(std::remove_if(g_clicks.begin(), g_clicks.end(),
                       [&](const ClickFx& c) { return now - c.t0 > FX_CLICK_LIFE_MS; }), g_clicks.end());
        trail = g_trail;
        clicks = g_clicks;
    }
    bool trailAct = g_s.trailOn && trail.size() >= 2;
    bool clickAct = g_s.clickOn && !clicks.empty();


    // 涟漪会一直扩�?clickSize（星火也是），包围盒必须按这个半径算，否则圆环会被窗口裁�?
    float padTrail = g_s.trailWidth * 4.f + 8.f;
    float padClick = g_s.clickSize * 1.15f + g_s.clickGlow * 0.3f + 10.f;
    float pad = std::max(padTrail, clickAct ? padClick : 0.f);
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    auto grow = [&](float x, float y) {
        minx = std::min(minx, x - pad); miny = std::min(miny, y - pad);
        maxx = std::max(maxx, x + pad); maxy = std::max(maxy, y + pad);
    };
    if (trailAct) for (const Particle& p : trail) grow(p.x, p.y);
    if (clickAct) for (const ClickFx& c : clicks) grow(c.x, c.y);

    int L = std::max(0, int(floorf(minx)) - 2);
    int T = std::max(0, int(floorf(miny)) - 2);
    int R = std::min(g_W - 1, int(ceilf(maxx)) + 2);
    int B = std::min(g_H - 1, int(ceilf(maxy)) + 2);
    if (R <= L || B <= T) { HideOverlay(); return; }

    // 尺寸/位置量化�?64px 网格：位置和大小不变�?ULW 只是重传像素，省掉每帧的窗口移动/重建
    auto snap = [](int v) { return (v / 64) * 64; };
    auto quant = [](int v) { return ((v + 63) / 64) * 64; };
    L = snap(L);
    T = snap(T);
    R = std::min(g_W - 1, L + quant(R - L + 1) - 1);
    B = std::min(g_H - 1, T + quant(B - T + 1) - 1);

    // 清局�?
    size_t rowBytes = size_t(R - L + 1) * 4;
    for (int y = T; y <= B; ++y)
        memset(g_bits + size_t(y) * g_W + L, 0, rowBytes);

    // 画（拖尾先、点击后�?
    DrawTrail(now, trail);
    DrawClicks(now, clicks);

    // present
    // 外部可能把窗口藏起来（Win+D 显示桌面、锁屏、切换用户），g_shown 会变成谎报 ——
    // 每帧用 IsWindowVisible 复核一次，藏了就重新亮出来，否则特效会“断”掉再也回不来。
    if (!IsWindowVisible(g_hwnd)) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        g_shown = true;
    }
    POINT ptDst{ g_VX + L, g_VY + T };
    POINT ptSrc{ L, T };
    SIZE  sz{ R - L + 1, B - T + 1 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    // �?32 帧重申一�?topmost：每帧都�?SetWindowPos 会让 DWM 反复重排，实测非常贵
    static int ztick = 0;
    if ((++ztick % 32) == 0)
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    UpdateLayeredWindow(g_hwnd, g_screenDC, &ptDst, &sz, g_memDC, &ptSrc, 0, &bf, ULW_ALPHA);
    static int dbgTick = 0;
    static double accMs = 0;
    accMs += NowMs() - now;
    if ((++dbgTick % 40) == 0) {
        LogF("frame trail=%zu clicks=%zu bbox=%d,%d %dx%d avg=%.2fms", trail.size(), clicks.size(),
             L, T, R - L + 1, B - T + 1, accMs / 40.0);
        accMs = 0;
    }
}

// ───────────────────────────── 输入 ─────────────────────────────
static void PollMovement() {
    if (!g_s.on || g_mode) return;
    POINT p{};
    if (!GetCursorPos(&p)) return;
    float x = float(p.x - g_VX), y = float(p.y - g_VY);
    double now = NowMs();
    Lock lk;
    if (g_s.trailOn) PushTrail(x, y, now);
    g_lastX = p.x; g_lastY = p.y;
}

// 点击轮询放在主线程：工作线程没有输入队列，GetAsyncKeyState 可能一直返�?0
static void PollClick() {
    static bool lastDown = false;
    if (!g_s.on || g_mode || !g_s.clickOn) return;
    POINT p{};
    if (!GetCursorPos(&p)) return;
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (down && !lastDown) {
        Lock lk;
        TryClick(float(p.x - g_VX), float(p.y - g_VY), NowMs(), "poll");
    }
    lastDown = down;
}

static DWORD WINAPI InputThread(LPVOID) {
    timeBeginPeriod(1);
    int n = 0;
    while (!g_stopInput) {
        PollMovement();
        if ((++n % 400) == 0)
            LogF("counters hook=%ld clicks=%ld raw=%ld", g_hookEvents, g_hookClicks, g_rawEvents);
        Sleep(4);
    }
    timeEndPeriod(1);
    return 0;
}

// 演示模式：不依赖任何输入注入，进程内自己生成轨迹/点击（用于自检与预览）
static void DemoTick(double now) {
    if (!g_mode) return;
    float cx = g_W * 0.5f, cy = g_H * 0.5f;
    static double lastClick = 0;
    Lock lk;
    if (g_mode == 1 || g_mode == 3) {
        float t = float(now) / 1000.f;
        float x = cx + cosf(t * 1.6f) * g_W * 0.18f + cosf(t * 3.1f) * g_W * 0.05f;
        float y = cy + sinf(t * 2.2f) * g_H * 0.16f + sinf(t * 4.3f) * g_H * 0.04f;
        PushTrail(x, y, now);
    }
    if (g_mode == 1 || g_mode == 2) {
        double gap = (g_mode == 2) ? 1000.0 : 1200.0;
        if (now - lastClick > gap) { lastClick = now; SpawnClick(cx, cy, now); }
    }
}

// 渲染线程：主线程只负责消息泵（低级钩子必须有真正�?GetMessage 循环才会派发�?
static DWORD WINAPI RenderThread(LPVOID) {
    timeBeginPeriod(1);
    double last = 0;
    while (!g_stopInput) {
        double t = NowMs();
        if (t - last < 12.0) { Sleep(3); continue; }
        last = t;
        static double lastCfg = 0;
        if (t - lastCfg > 300.0) {
            lastCfg = t;
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExA(g_cfgPath.c_str(), GetFileExInfoStandard, &fad)) {
                ULONGLONG stamp = (ULONGLONG(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                                  fad.ftLastWriteTime.dwLowDateTime;
                if (stamp != g_cfgStamp) { g_cfgStamp = stamp; LoadConfig(); }
            }
        }
        if (g_mode) DemoTick(t);
        Render();
    }
    timeEndPeriod(1);
    return 0;
}

// ───────────────────────────── 窗口 ─────────────────────────────
static void EnsureDib() {
    g_VX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_VY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_W  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_H  = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (g_W <= 0) g_W = 1920;
    if (g_H <= 0) g_H = 1080;

    g_screenDC = GetDC(nullptr);
    g_memDC = CreateCompatibleDC(g_screenDC);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_W;
    bi.bmiHeader.biHeight = -g_H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_bmp = CreateDIBSection(g_memDC, &bi, DIB_RGB_COLORS, (void**)&g_bits, nullptr, 0);
    SelectObject(g_memDC, g_bmp);
    memset(g_bits, 0, size_t(g_W) * g_H * 4);
}

// 自愈：每 400ms 复核一次“窗口还在、还是 topmost、DIB 尺寸跟得上当前桌面”
// 任何一条不成立就重建/重新置顶/重新显示 —— 否则会出现“程序还在、特效不画了”
static void RebuildDib() {
    if (g_bmp) { DeleteObject(g_bmp); g_bmp = nullptr; }
    if (g_memDC) { DeleteDC(g_memDC); g_memDC = nullptr; }
    if (g_screenDC) { ReleaseDC(nullptr, g_screenDC); g_screenDC = nullptr; }
    g_bits = nullptr;
    EnsureDib();
}

static void EnsureOverlaySane() {
    static double lastCheck = 0;
    double t = NowMs();
    if (t - lastCheck < 400.0) return;
    lastCheck = t;
    if (GetSystemMetrics(SM_CXVIRTUALSCREEN) != g_W || GetSystemMetrics(SM_CYVIRTUALSCREEN) != g_H ||
        GetSystemMetrics(SM_XVIRTUALSCREEN) != g_VX || GetSystemMetrics(SM_YVIRTUALSCREEN) != g_VY) {
        Lock lk;
        RebuildDib();
        LogF("dib rebuilt %dx%d at %d,%d", g_W, g_H, g_VX, g_VY);
    }
    LONG ex = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_TOPMOST)) {
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        LogF("overlay lost topmost, re-asserted");
    }
    if (g_shown && !IsWindowVisible(g_hwnd)) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        LogF("overlay was hidden, re-shown");
    }
    static double lastLog = 0;
    if (t - lastLog > 5000.0) {
        lastLog = t;
        LogF("hb vis=%d top=%d dib=%dx%d@%d,%d", int(IsWindowVisible(g_hwnd)),
             int((ex & WS_EX_TOPMOST) != 0), g_W, g_H, g_VX, g_VY);
    }
}


static void LoadAccent() {
    DWORD v = 0, cb = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor",
                     RRF_RT_REG_DWORD, nullptr, &v, &cb) == ERROR_SUCCESS) {
        float r = float(v & 0xFF) / 255.f;
        float g = float((v >> 8) & 0xFF) / 255.f;
        float b = float((v >> 16) & 0xFF) / 255.f;
        float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        if (lum > 0.25f && (r + g + b) > 0.6f) { g_accR = r; g_accG = g; g_accB = b; }
    }
}

// ───────────────────────────── 设置面板（自绘，GDI�?─────────────────────────────
enum {
    ID_ON = 1, ID_CLICK_ON, ID_CLICK_STYLE, ID_CLICK_SIZE, ID_CLICK_GLOW,
    ID_TRAIL_ON, ID_TRAIL_STYLE, ID_TRAIL_LIFE, ID_TRAIL_WIDTH, ID_TRAIL_GLOW,
    ID_OPACITY, ID_BLEND, ID_COLOR_MODE, ID_SWATCH, ID_RESET, ID_CLOSE, ID_TEST
};

struct SliderInfo { RECT r; float lo, hi; float* val; };
static std::map<int, SliderInfo> g_sliders;
static std::vector<std::pair<int, RECT>> g_hits;
static int  g_dragId = -1;
static int  g_hoverId = -1;
static int  g_dpi = 96;
static float g_scale = 1.f;
static HICON g_trayIcon = nullptr;
static HFONT g_fTitle = nullptr, g_fSec = nullptr, g_fLabel = nullptr, g_fSmall = nullptr;
static HWND  g_panel = nullptr;

static int S(int v) { return int(v * g_scale + 0.5f); }

static void FillRound(HDC dc, RECT r, int rad, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(br); DeleteObject(pen);
}

static void TextAt(HDC dc, int x, int y, const wchar_t* s, HFONT f, COLORREF c, int w = 0, int h = 0) {
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    if (w > 0) {
        RECT r{ x, y, x + w, y + (h > 0 ? h : S(20)) };
        DrawTextW(dc, s, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    } else {
        TextOutW(dc, x, y, s, (int)wcslen(s));
    }
    SelectObject(dc, of);
}

static void TextRight(HDC dc, int xr, int y, const wchar_t* s, HFONT f, COLORREF c) {
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    TextOutW(dc, xr - sz.cx, y, s, (int)wcslen(s));
    SelectObject(dc, of);
}

static void PushHit(int id, RECT r) { g_hits.emplace_back(id, r); }

static void DrawSwitch(HDC dc, int id, int x, int y, bool on) {
    int w = S(46), h = S(26);
    RECT r{ x, y, x + w, y + h };
    FillRound(dc, r, h / 2, on ? RGB(34, 199, 214) : RGB(58, 66, 78));
    int kx = on ? x + w - h + 3 : x + 3;
    RECT kr{ kx, y + 3, kx + h - 6, y + h - 3 };
    FillRound(dc, kr, (h - 6) / 2, RGB(245, 249, 252));
    PushHit(id, r);
}

static void DrawSeg(HDC dc, int id, int x, int y, int w, int h,
                    const wchar_t* const* items, int n, int cur) {
    FillRound(dc, RECT{ x, y, x + w, y + h }, S(9), RGB(44, 52, 63));
    int cw = w / n;
    for (int i = 0; i < n; ++i) {
        RECT r{ x + i * cw + S(2), y + S(2), x + (i + 1) * cw - S(2), y + h - S(2) };
        if (i == cur) FillRound(dc, r, S(7), RGB(34, 199, 214));
        HFONT f = (i == cur) ? g_fLabel : g_fSmall;
        HGDIOBJ of = SelectObject(dc, f);
        SetTextColor(dc, i == cur ? RGB(12, 40, 44) : RGB(206, 216, 226));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, items[i], -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(dc, of);
        PushHit(id * 100 + i, r);
    }
}

static void DrawSlider(HDC dc, int id, int x, int y, int w,
                       const wchar_t* label, const wchar_t* valueText,
                       float lo, float hi, float* val) {
    TextAt(dc, x, y, label, g_fLabel, RGB(222, 230, 238));
    TextRight(dc, x + w, y, valueText, g_fSmall, RGB(143, 196, 204));
    int ty = y + S(26);
    RECT track{ x, ty, x + w, ty + S(6) };
    FillRound(dc, track, S(3), RGB(52, 60, 72));
    float t = (*val - lo) / (hi - lo);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    int kx = x + int(t * w);
    RECT fill{ x, ty, kx, ty + S(6) };
    FillRound(dc, fill, S(3), RGB(34, 199, 214));
    RECT knob{ kx - S(7), ty - S(4), kx + S(7), ty + S(10) };
    FillRound(dc, knob, S(7), RGB(240, 248, 250));
    g_sliders[id] = { RECT{ x - S(4), ty - S(8), x + w + S(4), ty + S(14) }, lo, hi, val };
    PushHit(id, g_sliders[id].r);
}

static void DrawCardHeader(HDC dc, int x, int y, int w, const wchar_t* title, int switchId, bool on) {
    TextAt(dc, x, y + S(2), title, g_fSec, RGB(236, 242, 248));
    if (switchId) DrawSwitch(dc, switchId, x + w - S(46), y, on);
}

static void PaintPanel(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ ob = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(20, 24, 30));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    g_hits.clear();
    g_sliders.clear();

    int x = S(22), w = W - S(44), y = S(14);
    TextAt(mem, x, y, L"MouseTrailFx", g_fTitle, RGB(240, 246, 252));
    TextAt(mem, x, y + S(26), L"点击与拖尾效果 · 设置", g_fSmall, RGB(126, 138, 150));
    y += S(52);

    // 总开�?
    {
        RECT card{ x, y, x + w, y + S(44) };
        FillRound(mem, card, S(10), RGB(28, 34, 42));
        TextAt(mem, x + S(12), y + S(12), L"启用点击与拖尾效果", g_fLabel, RGB(228, 236, 244));
        DrawSwitch(mem, ID_ON, x + w - S(58), y + S(9), g_s.on);
        y += S(50);
    }

    // 点击效果
    {
        int h = S(186);
        RECT card{ x, y, x + w, y + h };
        FillRound(mem, card, S(10), RGB(28, 34, 42));
        int ix = x + S(12), iw = w - S(24), iy = y + S(8);
        DrawCardHeader(mem, ix, iy, iw, L"点击效果", ID_CLICK_ON, g_s.clickOn);
        iy += S(32);
        if (g_s.clickOn) {
            static const wchar_t* styles[] = { L"涟漪", L"星火", L"两者" };
            TextAt(mem, ix, iy, L"点击样式", g_fSmall, RGB(150, 162, 174));
            iy += S(16);
            DrawSeg(mem, ID_CLICK_STYLE, ix, iy, iw, S(30), styles, 3, g_s.clickStyle);
            iy += S(38);
            wchar_t buf[32];
            swprintf(buf, 32, L"%dpx", int(g_s.clickSize));
            DrawSlider(mem, ID_CLICK_SIZE, ix, iy, iw, L"半径", buf, 30.f, 400.f, &g_s.clickSize);
            iy += S(42);
            swprintf(buf, 32, L"%d%%", int(g_s.clickGlow));
            DrawSlider(mem, ID_CLICK_GLOW, ix, iy, iw, L"点击光晕", buf, 0.f, 100.f, &g_s.clickGlow);
        }
        y += h + S(8);
    }

    // 拖尾效果
    {
        int h = S(228);
        RECT card{ x, y, x + w, y + h };
        FillRound(mem, card, S(10), RGB(28, 34, 42));
        int ix = x + S(12), iw = w - S(24), iy = y + S(8);
        DrawCardHeader(mem, ix, iy, iw, L"拖尾效果", ID_TRAIL_ON, g_s.trailOn);
        iy += S(32);
        if (g_s.trailOn) {
            static const wchar_t* styles[] = { L"彗尾", L"星尘" };
            TextAt(mem, ix, iy, L"拖尾样式", g_fSmall, RGB(150, 162, 174));
            iy += S(16);
            DrawSeg(mem, ID_TRAIL_STYLE, ix, iy, iw, S(30), styles, 2, g_s.trailStyle);
            iy += S(38);
            wchar_t buf[32];
            swprintf(buf, 32, L"%dms", int(g_s.trailLife));
            DrawSlider(mem, ID_TRAIL_LIFE, ix, iy, iw, L"拖尾时长", buf, 80.f, 2000.f, &g_s.trailLife);
            iy += S(42);
            swprintf(buf, 32, L"%.0fpx", g_s.trailWidth);
            DrawSlider(mem, ID_TRAIL_WIDTH, ix, iy, iw, L"拖尾粗细", buf, 1.f, 12.f, &g_s.trailWidth);
            iy += S(42);
            swprintf(buf, 32, L"%d%%", int(g_s.trailGlow));
            DrawSlider(mem, ID_TRAIL_GLOW, ix, iy, iw, L"拖尾光晕", buf, 0.f, 100.f, &g_s.trailGlow);
        }
        y += h + S(8);
    }

    // 整层与配�?
    {
        int h = S(216);
        RECT card{ x, y, x + w, y + h };
        FillRound(mem, card, S(10), RGB(28, 34, 42));
        int ix = x + S(12), iw = w - S(24), iy = y + S(8);
        TextAt(mem, ix, iy, L"整层与配色", g_fSec, RGB(236, 242, 248));
        iy += S(30);
        wchar_t buf[32];
        swprintf(buf, 32, L"%d%%", int(g_s.opacity));
        DrawSlider(mem, ID_OPACITY, ix, iy, iw, L"不透明度", buf, 10.f, 100.f, &g_s.opacity);
        iy += S(42);

        static const wchar_t* blends[] = { L"滤色", L"正常", L"叠加", L"正片叠底", L"变亮" };
        TextAt(mem, ix, iy + S(2), L"混合模式", g_fLabel, RGB(222, 230, 238));
        RECT br{ ix + iw - S(96), iy - S(2), ix + iw, iy + S(22) };
        FillRound(mem, br, S(7), RGB(44, 52, 63));
        HGDIOBJ of = SelectObject(mem, g_fSmall);
        SetTextColor(mem, RGB(206, 216, 226));
        SetBkMode(mem, TRANSPARENT);
        DrawTextW(mem, blends[g_s.blend], -1, &br, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(mem, of);
        PushHit(ID_BLEND, br);
        iy += S(36);

        static const wchar_t* modes[] = { L"主题色", L"彩虹", L"自定义" };
        TextAt(mem, ix, iy, L"效果配色", g_fSmall, RGB(150, 162, 174));
        iy += S(16);
        DrawSeg(mem, ID_COLOR_MODE, ix, iy, iw, S(30), modes, 3, g_s.colorMode);
        iy += S(38);

        static const unsigned sw[] = { 0x4f8cff, 0x35d07f, 0xff5c8a, 0xffb020, 0xa06bff, 0x22c7d6, 0xffffff };
        int cx = ix;
        for (int i = 0; i < 7; ++i) {
            RECT r{ cx, iy, cx + S(26), iy + S(26) };
            HBRUSH b = CreateSolidBrush(RGB((sw[i] >> 16) & 0xFF, (sw[i] >> 8) & 0xFF, sw[i] & 0xFF));
            HPEN p = CreatePen(PS_SOLID, 1, (g_s.colorMode == 2 && g_s.color == sw[i]) ? RGB(255, 255, 255) : RGB(60, 68, 80));
            HGDIOBJ ob2 = SelectObject(mem, b), op2 = SelectObject(mem, p);
            Ellipse(mem, r.left, r.top, r.right, r.bottom);
            SelectObject(mem, ob2); SelectObject(mem, op2);
            DeleteObject(b); DeleteObject(p);
            PushHit(ID_SWATCH * 100 + i, r);
            cx += S(34);
        }
        y += h + S(8);
    }

    // 底部按钮：测�?/ 恢复默认 / 关闭
    {
        int bw = (w - S(12)) / 3;
        RECT r0{ x, y, x + bw, y + S(34) };
        RECT r1{ x + bw + S(6), y, x + 2 * bw + S(6), y + S(34) };
        RECT r2{ x + 2 * bw + S(12), y, x + w, y + S(34) };
        FillRound(mem, r0, S(9), RGB(38, 46, 56));
        FillRound(mem, r1, S(9), RGB(44, 52, 63));
        FillRound(mem, r2, S(9), RGB(34, 199, 214));
        HGDIOBJ of = SelectObject(mem, g_fLabel);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(150, 220, 228));
        DrawTextW(mem, L"测试点击", -1, &r0, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(mem, RGB(210, 220, 230));
        DrawTextW(mem, L"恢复默认", -1, &r1, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(mem, RGB(10, 36, 40));
        DrawTextW(mem, L"关闭", -1, &r2, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(mem, of);
        PushHit(ID_TEST, r0);
        PushHit(ID_RESET, r1);
        PushHit(ID_CLOSE, r2);
    }

    BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static int HitTest(POINT p) {
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it)
        if (PtInRect(&it->second, p)) return it->first;
    return 0;
}

static void SetSliderFromMouse(int id, int mx) {
    auto it = g_sliders.find(id);
    if (it == g_sliders.end()) return;
    SliderInfo& si = it->second;
    float t = float(mx - si.r.left) / float(std::max(1L, si.r.right - si.r.left));
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    *si.val = si.lo + t * (si.hi - si.lo);
}

static void ApplyHit(int id) {
    switch (id) {
        case ID_ON: g_s.on = !g_s.on; break;
        case ID_CLICK_ON: g_s.clickOn = !g_s.clickOn; break;
        case ID_TRAIL_ON: g_s.trailOn = !g_s.trailOn; break;
        case ID_BLEND: g_s.blend = (g_s.blend + 1) % 5; break;
        case ID_RESET: ApplyPreset(0); break;
        case ID_TEST: {
            POINT p{};
            GetCursorPos(&p);
            Lock lk;
            double now = NowMs();
            for (int i = 0; i < 3; ++i)
                SpawnClick(float(p.x - g_VX) + i * 40.f, float(p.y - g_VY) + i * 20.f, now + i * 130.0);
            LogF("panel test click");
            break;
        }
        case ID_CLOSE: ShowWindow(g_panel, SW_HIDE); break;
        default: break;
    }
    if (id >= ID_CLICK_STYLE * 100 && id < ID_CLICK_STYLE * 100 + 3) g_s.clickStyle = id % 100;
    if (id >= ID_TRAIL_STYLE * 100 && id < ID_TRAIL_STYLE * 100 + 2) g_s.trailStyle = id % 100;
    if (id >= ID_COLOR_MODE * 100 && id < ID_COLOR_MODE * 100 + 3) g_s.colorMode = id % 100;
    if (id >= ID_SWATCH * 100 && id < ID_SWATCH * 100 + 7) {
        static const unsigned sw[] = { 0x4f8cff, 0x35d07f, 0xff5c8a, 0xffb020, 0xa06bff, 0x22c7d6, 0xffffff };
        g_s.color = sw[id % 100];
        g_s.colorMode = 2;
    }
}

static LRESULT CALLBACK PanelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: PaintPanel(h); return 0;
        case WM_MOUSEMOVE: {
            POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (g_dragId >= 0) {
                SetSliderFromMouse(g_dragId, p.x);
                InvalidateRect(h, nullptr, FALSE);
            } else {
                int id = HitTest(p);
                if (id != g_hoverId) { g_hoverId = id; }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            SetFocus(h);
            POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int id = HitTest(p);
            if (g_sliders.count(id)) {
                g_dragId = id;
                SetCapture(h);
                SetSliderFromMouse(id, p.x);
                InvalidateRect(h, nullptr, FALSE);
            } else if (id) {
                ApplyHit(id);
                InvalidateRect(h, nullptr, FALSE);
                if (id == ID_ON || id == ID_CLICK_ON || id == ID_TRAIL_ON || id == ID_RESET)
                    SaveConfig();
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (g_dragId >= 0) {
                g_dragId = -1;
                ReleaseCapture();
                SaveConfig();
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) ShowWindow(h, SW_HIDE);
            return 0;
        case WM_CLOSE:
            ShowWindow(h, SW_HIDE);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void PanelCreate() {
    if (g_panel) return;
    HDC dc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    // 每显示器 v2 �?GetDeviceCaps 可能仍返�?96，优先用 GetDpiForSystem
    {
        using Fn = UINT (WINAPI*)();
        if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
            auto fn = (Fn)GetProcAddress(u, "GetDpiForSystem");
            if (fn) { UINT d = fn(); if (d >= 96 && d <= 480) g_dpi = int(d); }
        }
    }
    if (g_dpi <= 0) g_dpi = 96;

    // 面板高度自适应：优先按 DPI 缩放，但不得超出工作区（1080p 一屏放得下�?
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int workH = wa.bottom - wa.top;
    if (workH <= 0) workH = 1040;
    float want = float(g_dpi) / 96.f;
    float fit = float(workH - 40) / 820.f;
    g_scale = want < fit ? want : fit;
    if (g_scale < 0.72f) g_scale = 0.72f;
    LogF("panel dpi=%d scale=%.2f workH=%d", g_dpi, g_scale, workH);

    g_fTitle = CreateFontW(-S(22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
    g_fSec = CreateFontW(-S(16), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Segoe UI");
    g_fLabel = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");
    g_fSmall = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Segoe UI");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MTFXPanelWnd";
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    // 客户区高�?820，窗口高度要把标题栏算进去，否则底部按钮会被裁掉
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT wr{ 0, 0, S(470), S(820) };
    AdjustWindowRectEx(&wr, style, FALSE, WS_EX_TOOLWINDOW);
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    g_panel = CreateWindowExW(WS_EX_TOOLWINDOW, L"MTFXPanelWnd", L"MouseTrailFx 设置",
                              style,
                              (sx - w) / 2, (sy - h) / 2, w, h,
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    // 深色标题栏（Win10 1809+�?
    if (g_panel) {
        BOOL dark = TRUE;
        using DwmSet = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        if (HMODULE d = LoadLibraryW(L"dwmapi.dll")) {
            auto fn = (DwmSet)GetProcAddress(d, "DwmSetWindowAttribute");
            if (fn) fn(g_panel, 20, &dark, sizeof(dark));
            FreeLibrary(d);
        }
    }
}

static void PanelShow() {
    PanelCreate();
    ShowWindow(g_panel, SW_SHOW);
    SetForegroundWindow(g_panel);
    InvalidateRect(g_panel, nullptr, FALSE);
}

// ───────────────────────────── 托盘 ─────────────────────────────
#define WM_TRAY (WM_APP + 3)
#define IDM_ON 1001
#define IDM_PRESET_BASE 1100
#define IDM_SETTINGS 1200
#define IDM_OPENCFG 1201
#define IDM_AUTOSTART 1202
#define IDM_EXIT 1203
#define IDM_SHORTCUT 1204
#define IDM_CAPTURE 1206
#define IDM_TESTCLICK 1205

static NOTIFYICONDATAW g_nid{};

// 运行时画一个托盘图标：青色发光�?+ 白色核心�?2bpp �?alpha，CreateIconIndirect�?
static HICON MakeTrayIcon(int size) {
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);

    uint32_t* px = (uint32_t*)bits;
    float c = (size - 1) * 0.5f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float dx = x - c, dy = y - c;
            float d = sqrtf(dx * dx + dy * dy) / (size * 0.5f);
            float a = 1.f - d;
            a = a < 0.f ? 0.f : a;
            a = powf(a, 1.5f);
            float t = 1.f - std::min(1.f, d * 1.7f);     // 中心更白
            float r = (0.13f + (1.f - 0.13f) * t * 0.9f);
            float g = (0.78f + (1.f - 0.78f) * t * 0.9f);
            float b = (0.84f + (1.f - 0.84f) * t * 0.9f);
            a = std::min(1.f, a * 1.25f);
            px[y * size + x] = (uint32_t(255.f * a) << 24) |
                               (uint32_t(255.f * r * a) << 16) |
                               (uint32_t(255.f * g * a) << 8) |
                               uint32_t(255.f * b * a);
        }
    }

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return ic ? ic : LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
}

static void TrayAdd(HWND h) {
    g_trayIcon = MakeTrayIcon(32);
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = h;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_trayIcon;
    wcsncpy(g_nid.szTip, L"MouseTrailFx", 63);
    g_nid.szTip[63] = 0;
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

static void TrayUpdateTip(const wchar_t* tip) {
    wcsncpy(g_nid.szTip, tip, 127);
    g_nid.szTip[127] = 0;
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void ApplyCaptureAffinity() {
    const DWORD kExclude = 0x00000011;   // WDA_EXCLUDEFROMCAPTURE
    const DWORD kMonitor = 0x00000001;   // WDA_MONITOR (fallback)
    typedef BOOL (WINAPI* Fn)(HWND, DWORD);
    Fn fn = nullptr;
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) fn = (Fn)GetProcAddress(u, "SetWindowDisplayAffinity");
    if (!fn || !g_hwnd) return;
    BOOL ok = fn(g_hwnd, g_captureHidden ? kExclude : 0);
    if (!ok && g_captureHidden) ok = fn(g_hwnd, kMonitor);
    LogF("capture hidden=%d ok=%d", int(g_captureHidden), int(ok));
}

static void TrayMenu(HWND h) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (g_s.on ? MF_CHECKED : 0), IDM_ON, L"启用效果");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    HMENU pm = CreatePopupMenu();
    AppendMenuW(pm, MF_STRING, IDM_PRESET_BASE + 0, L"默认");
    AppendMenuW(pm, MF_STRING, IDM_PRESET_BASE + 1, L"霓虹");
    AppendMenuW(pm, MF_STRING, IDM_PRESET_BASE + 2, L"极简");
    AppendMenuW(pm, MF_STRING, IDM_PRESET_BASE + 3, L"星尘");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)pm, L"预设");
    AppendMenuW(m, MF_STRING, IDM_SETTINGS, L"设置…");
    AppendMenuW(m, MF_STRING, IDM_OPENCFG, L"打开配置文件");
    AppendMenuW(m, MF_STRING | (g_s.autostart ? MF_CHECKED : 0), IDM_AUTOSTART, L"开机自启");
    AppendMenuW(m, MF_STRING | (g_captureHidden ? MF_CHECKED : 0), IDM_CAPTURE, L"截图时隐藏特效");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_SHORTCUT, L"创建桌面快捷方式");
    AppendMenuW(m, MF_STRING, IDM_TESTCLICK, L"测试点击效果");
    AppendMenuW(m, MF_STRING, IDM_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(h);
    int cmd = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, h, nullptr);
    DestroyMenu(m);
    if (!cmd) return;
    if (cmd == IDM_ON) { g_s.on = !g_s.on; SaveConfig(); }
    else if (cmd >= IDM_PRESET_BASE && cmd < IDM_PRESET_BASE + 10) { ApplyPreset(cmd - IDM_PRESET_BASE); }
    else if (cmd == IDM_SETTINGS) { PanelShow(); }
    else if (cmd == IDM_OPENCFG) {
        wchar_t wp[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, g_cfgPath.c_str(), -1, wp, MAX_PATH);
        ShellExecuteW(nullptr, L"open", L"notepad.exe", wp, nullptr, SW_SHOW);
    }
    else if (cmd == IDM_AUTOSTART) { g_s.autostart = !g_s.autostart; ApplyAutostart(g_s.autostart); SaveConfig(); }
    else if (cmd == IDM_SHORTCUT) { MakeShortcuts(); }
    else if (cmd == IDM_CAPTURE) { g_captureHidden = !g_captureHidden; ApplyCaptureAffinity(); SaveConfig(); }
    else if (cmd == IDM_TESTCLICK) {
        // 在光标处放三个涟漪：用来确认"渲染没问题，只是没收到点�?
        POINT p{};
        GetCursorPos(&p);
        Lock lk;
        double now = NowMs();
        for (int i = 0; i < 3; ++i)
            SpawnClick(float(p.x - g_VX) + i * 40.f, float(p.y - g_VY) + i * 20.f, now + i * 120.0);
        LogF("test click x3");
    }
    else if (cmd == IDM_EXIT) { PostQuitMessage(0); }
}

static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        InterlockedIncrement(&g_hookEvents);
        if (wParam == WM_LBUTTONDOWN) {
            InterlockedIncrement(&g_hookClicks);
            const MSLLHOOKSTRUCT* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
            InterlockedExchange(&g_hookX, ms->pt.x);
            InterlockedExchange(&g_hookY, ms->pt.y);
            if (g_hwnd) PostMessageW(g_hwnd, WM_APP + 1, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_showMsg && msg == g_showMsg) { PanelShow(); return 0; }
    if (g_taskbarMsg && msg == g_taskbarMsg) { TrayAdd(h); return 0; }
    switch (msg) {
        case WM_INPUT: {
            InterlockedIncrement(&g_rawEvents);
            UINT size = sizeof(RAWINPUT);
            RAWINPUT ri{};
            if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1
                && ri.header.dwType == RIM_TYPEMOUSE) {
                if (ri.data.mouse.usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) {
                    POINT p{};
                    GetCursorPos(&p);
                    if (g_s.on && g_s.clickOn) {
                        Lock lk;
                        TryClick(float(p.x - g_VX), float(p.y - g_VY), NowMs(), "raw");
                    }
                }
            }
            return DefWindowProcW(h, msg, wp, lp);
        }
        case WM_APP + 1: {
            if (g_s.on && g_s.clickOn) {
                Lock lk;
                TryClick(float(g_hookX - g_VX), float(g_hookY - g_VY), NowMs(), "hook");
            }
            return 0;
        }
        case WM_TIMER:
            if (wp == 2) {
                PollClick();
                return 0;
            }
            return 0;
        case WM_TRAY:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) TrayMenu(h);
            else if (LOWORD(lp) == WM_LBUTTONDBLCLK) PanelShow();
            return 0;
        case WM_HOTKEY:
            if (wp == 1) { PostQuitMessage(0); return 0; }
            if (wp == 2) { g_s.on = !g_s.on; if (!g_s.on) { g_trail.clear(); g_clicks.clear(); HideOverlay(); } return 0; }
            if (wp == 3) { PanelShow(); return 0; }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    QueryPerformanceFrequency(&g_freq);
    LARGE_INTEGER c0;
    QueryPerformanceCounter(&c0);
    g_t0 = c0.QuadPart;
    g_mode = 0;
    if (wcsstr(GetCommandLineW(), L"--freeze")) g_mode = 4;
    else if (wcsstr(GetCommandLineW(), L"--clicktest")) g_mode = 2;
    else if (wcsstr(GetCommandLineW(), L"--trailtest")) g_mode = 3;
    else if (wcsstr(GetCommandLineW(), L"--demo")) g_mode = 1;
    LogF("start pid=%lu mode=%d", GetCurrentProcessId(), g_mode);

    ConfigPathInit();
    // --config <path> 覆盖，便于测�?
    {
        std::wstring cmd = GetCommandLineW();
        size_t pos = cmd.find(L"--config");
        if (pos != std::wstring::npos) {
            size_t q1 = cmd.find(L'"', pos), q2 = std::wstring::npos;
            if (q1 != std::wstring::npos) q2 = cmd.find(L'"', q1 + 1);
            if (q1 != std::wstring::npos && q2 != std::wstring::npos && q2 > q1 + 1) {
                std::wstring p = cmd.substr(q1 + 1, q2 - q1 - 1);
                char buf[MAX_PATH];
                WideCharToMultiByte(CP_ACP, 0, p.c_str(), -1, buf, MAX_PATH, nullptr, nullptr);
                g_cfgPath = buf;
            }
        }
    }
    LoadConfig();
    g_s.autostart = IsAutostartOn();
    bool firstRun = (GetFileAttributesA(g_cfgPath.c_str()) == INVALID_FILE_ATTRIBUTES);
    if (firstRun) SaveConfig();
    LogF("config path: %s", g_cfgPath.c_str());

    // 单实例：第二次启动只负责把已有实例的设置面板叫出�?
    g_showMsg = RegisterWindowMessageW(L"MouseTrailFx_ShowPanel");
    g_taskbarMsg = RegisterWindowMessageW(L"TaskbarCreated");
    {
        HANDLE inst = CreateMutexW(nullptr, FALSE, L"MouseTrailFx_SingleInstance");
        if (inst && GetLastError() == ERROR_ALREADY_EXISTS) {
            if (wcsstr(GetCommandLineW(), L"--shortcut")) {
                HRESULT hc = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                LogF("coinit hr=0x%08lx", (unsigned long)hc);
                MakeShortcuts();
            }
            HWND other = FindWindowW(L"MouseTrailFxWnd", nullptr);
            if (other) {
                PostMessageW(other, g_showMsg, 0, 0);
            } else {
                MessageBoxW(nullptr, L"MouseTrailFx 已在运行，请查看托盘图标。", L"MouseTrailFx",
                            MB_OK | MB_ICONINFORMATION);
            }
            return 0;
        }
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // per-monitor v2 DPI（Win10 1703+，动态取�?
    using SetCtx = BOOL (WINAPI*)(void*);
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        auto fn = (SetCtx)GetProcAddress(u, "SetProcessDpiAwarenessContext");
        if (fn) fn((void*)-4); // PER_MONITOR_AWARE_V2
    }

    timeBeginPeriod(1);
    InitializeCriticalSection(&g_lock);
    LoadAccent();
    EnsureDib();

    if (g_mode == 4) {   // 冻结展示：固定一�?S 形拖�?+ 一�?30% 进度的涟漪，便于截图对比
        g_freezeT = NowMs();
        float cx = g_W * 0.5f, cy = g_H * 0.5f;
        for (int i = 0; i < 80; ++i) {
            float p = float(i) / 79.f;
            float x = cx - 520.f + p * 900.f;
            float y = cy + 120.f * sinf(p * 3.14159f * 1.6f) - 40.f;
            g_trail.push_back({x, y, g_freezeT - (79 - i) * 5.2, 0.f});
        }
        g_clicks.push_back({cx + 300.f, cy - 60.f, g_freezeT - 270.f, 7, 0.f});
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"MouseTrailFxWnd";
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"MouseTrailFxWnd", L"MouseTrailFx", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    ApplyCaptureAffinity();

    RegisterHotKey(g_hwnd, 1, MOD_CONTROL | MOD_ALT, 'Q');
    RegisterHotKey(g_hwnd, 2, MOD_CONTROL | MOD_ALT, 'F');
    RegisterHotKey(g_hwnd, 3, MOD_CONTROL | MOD_ALT, 'P');

    SetTimer(g_hwnd, 2, 8, nullptr);   // 主线程轮询左键（工作线程没有输入队列�?
    TrayAdd(g_hwnd);

    g_hMouse = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    LogF("hook=%p err=%lu", (void*)g_hMouse, GetLastError());

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = g_hwnd;
    BOOL rawOk = RegisterRawInputDevices(&rid, 1, sizeof(rid));
    LogF("rawinput=%d err=%lu", int(rawOk), GetLastError());

    HANDLE hInput = CreateThread(nullptr, 0, InputThread, nullptr, 0, nullptr);
    HANDLE hRender = CreateThread(nullptr, 0, RenderThread, nullptr, 0, nullptr);

    if (wcsstr(GetCommandLineW(), L"--panel")) PanelShow();
    // 普通双击启动：直接把面板亮出来（否则进程藏在托盘里，用户会以为没反应）
    {
        std::wstring cl = GetCommandLineW();
        bool silent = cl.find(L"/silent") != std::wstring::npos;
        bool hasFlag = cl.find(L"--") != std::wstring::npos;
        if (!hasFlag && !silent && !g_mode) PanelShow();
    }
    if (firstRun && !g_mode) PanelShow();          // 首次启动直接把设置面板摆出来
    if (wcsstr(GetCommandLineW(), L"--shortcut")) MakeShortcuts();

    if (wcsstr(GetCommandLineW(), L"--dumpicon")) {
        HICON ic = MakeTrayIcon(64);
        ICONINFO ii{};
        if (ic && GetIconInfo(ic, &ii) && ii.hbmColor) {
            BITMAP bm{};
            GetObject(ii.hbmColor, sizeof(bm), &bm);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = bm.bmWidth;
            bi.bmiHeader.biHeight = bm.bmHeight;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            int stride = bm.bmWidth * 4;
            std::vector<unsigned char> buf(size_t(stride) * bm.bmHeight);
            HDC sdc = GetDC(nullptr);
            GetDIBits(sdc, ii.hbmColor, 0, bm.bmHeight, buf.data(), &bi, DIB_RGB_COLORS);
            ReleaseDC(nullptr, sdc);
            char path[MAX_PATH];
            GetModuleFileNameA(nullptr, path, MAX_PATH);
            char* sl = strrchr(path, '\\');
            if (sl) *sl = 0;
            strcat(path, "\\icon_dump.bmp");
            FILE* f = fopen(path, "wb");
            if (f) {
                BITMAPFILEHEADER fh{};
                BITMAPINFOHEADER ih = bi.bmiHeader;
                fh.bfType = 0x4D42;
                fh.bfOffBits = sizeof(fh) + sizeof(ih);
                fh.bfSize = fh.bfOffBits + DWORD(buf.size());
                fwrite(&fh, sizeof(fh), 1, f);
                fwrite(&ih, sizeof(ih), 1, f);
                fwrite(buf.data(), 1, buf.size(), f);
                fclose(f);
                LogF("icon dumped %dx%d", bm.bmWidth, bm.bmHeight);
            }
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            DeleteObject(ii.hbmColor);
        }
    }

    if (wcsstr(GetCommandLineW(), L"--selftest")) {
        PanelShow();
        UpdateWindow(g_panel);
        POINT p1{ S(365), S(235) };   // 「两者」分�?
        POINT p2{ S(100), S(180) };   // 半径滑杆
        int id1 = HitTest(p1), id2 = HitTest(p2);
        LogF("selftest hit seg=%d slider=%d sliders=%zu", id1, id2, g_sliders.size());
        if (id1) ApplyHit(id1);
        if (id2) SetSliderFromMouse(id2, p2.x);
        SaveConfig();
        LogF("selftest after: clickStyle=%d size=%.0f", g_s.clickStyle, g_s.clickSize);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    InterlockedExchange(&g_stopInput, 1);
    LogF("exit normal");
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_hMouse) UnhookWindowsHookEx(g_hMouse);
    if (hInput) { WaitForSingleObject(hInput, 500); CloseHandle(hInput); }
    if (hRender) { WaitForSingleObject(hRender, 500); CloseHandle(hRender); }
    timeEndPeriod(1);
    DeleteCriticalSection(&g_lock);
    return 0;
}

