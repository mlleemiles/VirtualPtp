// MiddlePtp.cpp - hold the middle mouse button and move the mouse = two-finger scroll
// on the VirtualPtp virtual Precision Touchpad. Windows' own touchpad engine does the
// scrolling, so you get native smooth scrolling + inertia everywhere (WinUI/UWP included).
//
// Build (x64 Native Tools Command Prompt for VS):
//   cl /nologo /O2 /EHsc /std:c++17 /W4 /DUNICODE /D_UNICODE MiddlePtp.cpp ^
//      /link /SUBSYSTEM:WINDOWS user32.lib shell32.lib hid.lib setupapi.lib winmm.lib

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <mmsystem.h>
extern "C" {
#include <hidsdi.h>
}

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>

#include "../driver/VirtualPtpShared.h"

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
constexpr ULONG_PTR kInjectTag   = 0x56505450;   // marks our own SendInput events
constexpr UINT WM_APP_TRAY       = WM_APP + 1;
constexpr UINT WM_APP_CLICK      = WM_APP + 2;
constexpr UINT WM_APP_STATUS     = WM_APP + 3;

constexpr double kMargin    = 60.0;    // keep fingers this far (touchpad units) from the edges
constexpr double kTapTravel = 120.0;   // ~3 mm: below this, a short contact could look like a tap

enum : UINT {
    ID_STATUS = 100, ID_ENABLED, ID_INVERT, ID_AXISLOCK, ID_EDIT_INI, ID_RELOAD, ID_EXIT,
    ID_SENS_BASE = 200,
};
const double kSensPresets[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0};

// ---------------------------------------------------------------------------
// Settings (atomics: written on the UI thread, read by the worker)
// ---------------------------------------------------------------------------
std::atomic<bool>   g_enabled{true};
std::atomic<double> g_sensitivity{1.0};    // touchpad units (1/1000 inch) per mouse count
std::atomic<bool>   g_invert{false};
std::atomic<bool>   g_axisLock{false};
std::atomic<int>    g_deadzone{6};         // mouse counts before fingers touch down
std::atomic<int>    g_clickTimeoutMs{400}; // press+release without moving within this = middle click
std::atomic<int>    g_minContactMs{250};   // short + tiny gestures are held this long (avoids 2-finger tap)
std::atomic<int>    g_frameIntervalMs{8};  // ~125 Hz, like a real touchpad
std::atomic<int>    g_fingerSpacing{700};  // distance between the two fingers (~18 mm)
std::vector<std::wstring> g_excluded;      // lower-case exe names; UI thread only
unsigned g_excludedGen = 0;
std::wstring g_iniPath;

// ---------------------------------------------------------------------------
// Input state (hook + raw input run on the UI thread; the worker consumes it)
// ---------------------------------------------------------------------------
std::atomic<bool>      g_held{false};
std::atomic<bool>      g_swallowUp{false};
std::atomic<unsigned>  g_pressSeq{0};     // incremented on every captured middle press
std::atomic<unsigned>  g_releaseSeq{0};   // = g_pressSeq of the press that was released
std::atomic<bool>      g_rawSeen{false};
std::atomic<long>      g_rawDx{0}, g_rawDy{0};
std::atomic<long>      g_hookDx{0}, g_hookDy{0};
std::atomic<ULONGLONG> g_pressTick{0};
POINT                  g_anchor{};

std::atomic<bool> g_deviceReady{false};
std::atomic<bool> g_quit{false};
HANDLE g_wakeEvent  = nullptr;
HANDLE g_quitEvent  = nullptr;
HWND   g_hwnd       = nullptr;
HHOOK  g_hook       = nullptr;
UINT   g_taskbarCreated = 0;

// ---------------------------------------------------------------------------
// Settings file
// ---------------------------------------------------------------------------
constexpr wchar_t kSection[] = L"MiddlePtp";

std::wstring IniGet(const wchar_t* key, const wchar_t* def) {
    wchar_t buf[4096];
    GetPrivateProfileStringW(kSection, key, def, buf, ARRAYSIZE(buf), g_iniPath.c_str());
    return buf;
}
int IniInt(const wchar_t* key, int def) {
    return (int)GetPrivateProfileIntW(kSection, key, def, g_iniPath.c_str());
}
void IniPut(const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(kSection, key, value.c_str(), g_iniPath.c_str());
}

std::wstring ExcludedToString() {
    std::wstring s;
    for (auto& e : g_excluded) { if (!s.empty()) s += L','; s += e; }
    return s;
}

void SaveSettings() {
    wchar_t num[64];
    IniPut(L"Enabled", g_enabled ? L"1" : L"0");
    swprintf_s(num, L"%.3g", g_sensitivity.load());
    IniPut(L"Sensitivity", num);
    IniPut(L"Invert", g_invert ? L"1" : L"0");
    IniPut(L"AxisLock", g_axisLock ? L"1" : L"0");
    IniPut(L"DeadzoneCounts", std::to_wstring(g_deadzone.load()));
    IniPut(L"ClickTimeoutMs", std::to_wstring(g_clickTimeoutMs.load()));
    IniPut(L"MinContactMs", std::to_wstring(g_minContactMs.load()));
    IniPut(L"FrameIntervalMs", std::to_wstring(g_frameIntervalMs.load()));
    IniPut(L"FingerSpacing", std::to_wstring(g_fingerSpacing.load()));
    IniPut(L"ExcludedApps", ExcludedToString());
}

void LoadSettings() {
    if (GetFileAttributesW(g_iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        g_excluded = {L"blender.exe"};
        SaveSettings();   // write defaults so the user has something to edit
    }
    g_enabled         = IniInt(L"Enabled", 1) != 0;
    g_sensitivity     = std::clamp(_wtof(IniGet(L"Sensitivity", L"1.0").c_str()), 0.05, 20.0);
    g_invert          = IniInt(L"Invert", 0) != 0;
    g_axisLock        = IniInt(L"AxisLock", 0) != 0;
    g_deadzone        = std::clamp(IniInt(L"DeadzoneCounts", 6), 0, 200);
    g_clickTimeoutMs  = std::clamp(IniInt(L"ClickTimeoutMs", 400), 0, 5000);
    g_minContactMs    = std::clamp(IniInt(L"MinContactMs", 250), 0, 1000);
    g_frameIntervalMs = std::clamp(IniInt(L"FrameIntervalMs", 8), 2, 20);
    g_fingerSpacing   = std::clamp(IniInt(L"FingerSpacing", 700), 300, 2000);

    g_excluded.clear();
    std::wstring list = IniGet(L"ExcludedApps", L"");
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(L',', start);
        if (end == std::wstring::npos) end = list.size();
        std::wstring item = list.substr(start, end - start);
        item.erase(0, item.find_first_not_of(L" \t"));
        item.erase(item.find_last_not_of(L" \t") + 1);
        std::transform(item.begin(), item.end(), item.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
        if (!item.empty()) g_excluded.push_back(item);
        start = end + 1;
    }
    ++g_excludedGen;
}

// ---------------------------------------------------------------------------
// Per-app exclusion (runs inside the mouse hook, so it's cached and cheap)
// ---------------------------------------------------------------------------
bool IsExcludedAt(POINT pt) {
    if (g_excluded.empty()) return false;
    HWND w = WindowFromPoint(pt);
    if (!w) return false;
    w = GetAncestor(w, GA_ROOT);
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);

    static DWORD    cachedPid = 0;
    static unsigned cachedGen = ~0u;
    static bool     cachedResult = false;
    if (pid == cachedPid && cachedGen == g_excludedGen) return cachedResult;

    bool excluded = false;
    if (HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t path[MAX_PATH];
        DWORD len = ARRAYSIZE(path);
        if (QueryFullProcessImageNameW(proc, 0, path, &len)) {
            std::wstring name(path, len);
            name = name.substr(name.find_last_of(L"\\/") + 1);
            std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
            excluded = std::find(g_excluded.begin(), g_excluded.end(), name) != g_excluded.end();
        }
        CloseHandle(proc);
    }
    cachedPid = pid;
    cachedGen = g_excludedGen;
    cachedResult = excluded;
    return excluded;
}

// ---------------------------------------------------------------------------
// Connection to the driver's vendor collection
// ---------------------------------------------------------------------------
class InjectDevice {
public:
    ~InjectDevice() { Close(); }
    bool Valid() const { return handle_ != INVALID_HANDLE_VALUE; }

    bool Open() {
        Close();
        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);
        HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return false;

        SP_DEVICE_INTERFACE_DATA ifData{sizeof(ifData)};
        for (DWORD i = 0; !Valid() && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &ifData); ++i) {
            DWORD needed = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &ifData, nullptr, 0, &needed, nullptr);
            if (needed == 0) continue;
            std::vector<BYTE> buf(needed);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &ifData, detail, needed, nullptr, nullptr)) continue;

            HANDLE h = CreateFileW(detail->DevicePath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;   // keyboards/mice/touchpads are exclusive; fine

            bool match = false;
            HIDD_ATTRIBUTES attr{sizeof(attr)};
            if (HidD_GetAttributes(h, &attr) && attr.VendorID == VPTP_VENDOR_ID && attr.ProductID == VPTP_PRODUCT_ID) {
                PHIDP_PREPARSED_DATA pp = nullptr;
                if (HidD_GetPreparsedData(h, &pp)) {
                    HIDP_CAPS caps{};
                    match = HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS &&
                            caps.UsagePage == VPTP_VENDOR_USAGE_PAGE && caps.Usage == VPTP_VENDOR_USAGE &&
                            caps.OutputReportByteLength == VPTP_REPORT_SIZE;
                    HidD_FreePreparsedData(pp);
                }
            }
            if (match) handle_ = h; else CloseHandle(h);
        }
        SetupDiDestroyDeviceInfoList(set);
        return Valid();
    }

    void Close() {
        if (Valid()) CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }

    bool Write(const PTP_FRAME& frame) {
        if (!Valid()) return false;
        BYTE report[VPTP_REPORT_SIZE];
        report[0] = REPORTID_INJECT;
        memcpy(report + 1, &frame, sizeof(frame));
        DWORD written = 0;
        if (!WriteFile(handle_, report, sizeof(report), &written, nullptr)) {
            Close();
            return false;
        }
        return true;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

// ---------------------------------------------------------------------------
// Gesture worker
// ---------------------------------------------------------------------------
enum class GState { Idle, Armed, Touching, Relifting };

struct Gesture {
    GState    state = GState::Idle;
    double    pendX = 0, pendY = 0;     // movement while armed (mouse counts)
    double    x = 0, y = 0;             // centre between the two fingers (touchpad units)
    double    dirX = 0, dirY = 1;       // last finger direction (for re-centring)
    double    carryX = 0, carryY = 0;   // movement accumulated while re-centring
    double    travel = 0;               // total finger travel this gesture
    int       axis = 0;                 // 0 free, 1 horizontal only, 2 vertical only
    ULONGLONG pressTick = 0, downTick = 0;
    bool      releasing = false;
    UCHAR     idBase = 0;
    unsigned  seq = 0;                  // which press this gesture belongs to
};

USHORT ScanTime() {
    static const LONGLONG freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    // exact integer conversion to 100 us units (no double precision loss after long uptimes)
    const unsigned long long ticks =
        (unsigned long long)(c.QuadPart / freq) * 10000ull +
        (unsigned long long)((c.QuadPart % freq) * 10000 / freq);
    return (USHORT)(ticks & 0xFFFF);
}

double RangeX() { return PTP_LOGICAL_MAX_X / 2.0 - g_fingerSpacing / 2.0 - kMargin; }
double RangeY() { return PTP_LOGICAL_MAX_Y / 2.0 - kMargin; }

bool SendPair(InjectDevice& dev, const Gesture& g, bool tip) {
    PTP_FRAME f{};
    const double half = g_fingerSpacing / 2.0;
    for (int i = 0; i < 2; ++i) {
        const double fx = g.x + (i == 0 ? -half : half);
        f.Contacts[i].Flags = (UCHAR)(PTP_FLAG_CONFIDENCE | (tip ? PTP_FLAG_TIP : 0) |
                                      ((g.idBase + i) << PTP_CONTACT_ID_SHIFT));
        f.Contacts[i].X = (USHORT)std::clamp(std::lround(fx), 0L, (long)PTP_LOGICAL_MAX_X);
        f.Contacts[i].Y = (USHORT)std::clamp(std::lround(g.y), 0L, (long)PTP_LOGICAL_MAX_Y);
    }
    f.ScanTime     = ScanTime();
    f.ContactCount = 2;
    f.Buttons      = 0;
    return dev.Write(f);
}

// Put the fingers down on the side opposite to the direction of travel, so a long
// swipe has as much room as possible before we have to lift and re-centre.
bool TouchDown(InjectDevice& dev, Gesture& g, double dirX, double dirY, ULONGLONG now, bool first) {
    const double len = std::hypot(dirX, dirY);
    const double ux = len > 0 ? dirX / len : 0, uy = len > 0 ? dirY / len : 0;
    g.x = PTP_LOGICAL_MAX_X / 2.0 - ux * 0.75 * RangeX();
    g.y = PTP_LOGICAL_MAX_Y / 2.0 - uy * 0.75 * RangeY();
    g.idBase = g.idBase == 0 ? 2 : 0;   // fresh contact ids for each touch-down
    g.state = GState::Touching;
    if (first) { g.downTick = now; g.travel = 0; }
    return SendPair(dev, g, true);
}

// Apply mouse movement to the fingers. Returns true if the fingers hit the pad edge.
bool MoveFingers(Gesture& g, double dx, double dy) {
    const double s = g_sensitivity * (g_invert ? -1.0 : 1.0);
    double mx = dx * s + g.carryX, my = dy * s + g.carryY;
    g.carryX = g.carryY = 0;
    if (g.axis == 1) my = 0;
    if (g.axis == 2) mx = 0;
    g.x += mx;
    g.y += my;
    g.travel += std::hypot(mx, my);
    if (mx != 0 || my != 0) { g.dirX = mx; g.dirY = my; }

    const double cx = PTP_LOGICAL_MAX_X / 2.0, cy = PTP_LOGICAL_MAX_Y / 2.0;
    const double rx = RangeX(), ry = RangeY();
    const bool out = g.x < cx - rx || g.x > cx + rx || g.y < cy - ry || g.y > cy + ry;
    g.x = std::clamp(g.x, cx - rx, cx + rx);
    g.y = std::clamp(g.y, cy - ry, cy + ry);
    return out;
}

// One frame tick. Returns false if the device went away.
bool Step(InjectDevice& dev, Gesture& g) {
    // Prefer raw (unaccelerated) counts; fall back to hook deltas if raw input never arrived.
    const long rdx = g_rawDx.exchange(0), rdy = g_rawDy.exchange(0);
    const long hdx = g_hookDx.exchange(0), hdy = g_hookDy.exchange(0);
    const bool raw = g_rawSeen.load();
    const double dx = raw ? rdx : hdx, dy = raw ? rdy : hdy;
    // Released if this press was let go, or a newer press already started.
    const bool released = g_releaseSeq == g.seq || g_pressSeq != g.seq;
    const ULONGLONG now = GetTickCount64();
    bool ok = true;

    switch (g.state) {
    case GState::Idle:
        break;

    case GState::Armed:
        g.pendX += dx;
        g.pendY += dy;
        if (std::hypot(g.pendX, g.pendY) >= g_deadzone && (g.pendX != 0 || g.pendY != 0)) {
            g.axis = !g_axisLock ? 0 : (std::abs(g.pendX) >= std::abs(g.pendY) ? 1 : 2);
            const double sgn = g_invert ? -1.0 : 1.0;
            double dirX = g.pendX * sgn, dirY = g.pendY * sgn;
            if (g.axis == 1) dirY = 0;
            if (g.axis == 2) dirX = 0;
            g.carryX = g.carryY = 0;
            ok = TouchDown(dev, g, dirX, dirY, now, true);
            g.releasing = released;
        } else if (released) {
            if (now - g.pressTick <= (ULONGLONG)g_clickTimeoutMs) PostMessageW(g_hwnd, WM_APP_CLICK, 0, 0);
            g.state = GState::Idle;
        }
        break;

    case GState::Touching: {
        if (released) g.releasing = true;
        const bool hitEdge = MoveFingers(g, dx, dy);
        if (g.releasing) {
            // A very short, tiny two-finger contact is a "two-finger tap" (= right click) to
            // Windows. Keep such contacts down a little longer; lift everything else at once
            // so Windows sees the real release velocity and applies inertia.
            const bool tapRisk = now - g.downTick < (ULONGLONG)g_minContactMs && g.travel < kTapTravel;
            if (!tapRisk) {
                ok = SendPair(dev, g, false);
                g.state = GState::Idle;
                break;
            }
        }
        ok = SendPair(dev, g, true);
        if (ok && hitEdge && !g.releasing) {
            ok = SendPair(dev, g, false);   // lift; re-centre on the next tick
            g.state = GState::Relifting;
        }
        break;
    }

    case GState::Relifting: {
        if (released || g.releasing) { g.state = GState::Idle; break; }
        const double s = g_sensitivity * (g_invert ? -1.0 : 1.0);
        ok = TouchDown(dev, g, g.dirX, g.dirY, now, false);
        g.carryX += dx * s;
        g.carryY += dy * s;
        break;
    }
    }
    return ok;
}

DWORD WINAPI WorkerMain(void*) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    bool lowResTimer = false;
    if (!timer) {   // pre-1803 Windows
        timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        timeBeginPeriod(1);
        lowResTimer = true;
    }

    InjectDevice dev;
    Gesture g;

    auto refreshDevice = [&] {
        const bool was = g_deviceReady;
        const bool now = dev.Valid() || dev.Open();
        g_deviceReady = now;
        if (was != now) PostMessageW(g_hwnd, WM_APP_STATUS, 0, 0);
    };
    refreshDevice();

    while (!g_quit) {
        if (g.state == GState::Idle) {
            HANDLE waits[] = {g_wakeEvent, g_quitEvent};
            WaitForMultipleObjects(2, waits, FALSE, 2000);   // also retries the device every 2 s
            if (g_quit) break;
            refreshDevice();
            const unsigned seq = g_pressSeq;
            if (seq == g.seq || !dev.Valid()) continue;
            const UCHAR lastIdBase = g.idBase;
            g = Gesture{};
            g.idBase = lastIdBase;
            g.seq = seq;
            g.state = GState::Armed;
            g.pressTick = g_pressTick;
        }

        LARGE_INTEGER due;
        due.QuadPart = -10000LL * g_frameIntervalMs;
        SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
        HANDLE waits[] = {timer, g_quitEvent};
        if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) break;

        if (!Step(dev, g)) {
            g.state = GState::Idle;
            refreshDevice();
        }
    }

    if (g.state == GState::Touching && dev.Valid()) SendPair(dev, g, false);   // never leave fingers down
    if (lowResTimer) timeEndPeriod(1);
    CloseHandle(timer);
    return 0;
}

// ---------------------------------------------------------------------------
// Input capture (UI thread)
// ---------------------------------------------------------------------------
void SignalRelease() {
    if (g_held.exchange(false)) {
        g_releaseSeq = g_pressSeq.load();
        SetEvent(g_wakeEvent);
    }
}

LRESULT CALLBACK MouseHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        if (m->dwExtraInfo != kInjectTag) {
            switch (wParam) {
            case WM_MBUTTONDOWN:
                if (g_held) return 1;
                if (g_enabled && g_deviceReady && !IsExcludedAt(m->pt)) {
                    g_anchor = m->pt;
                    g_rawSeen = false;
                    g_rawDx = 0; g_rawDy = 0; g_hookDx = 0; g_hookDy = 0;
                    g_pressTick = GetTickCount64();
                    g_held = true;
                    g_swallowUp = true;
                    ++g_pressSeq;
                    SetEvent(g_wakeEvent);
                    return 1;
                }
                break;
            case WM_MBUTTONUP:
                if (g_swallowUp.exchange(false)) {
                    SignalRelease();
                    return 1;
                }
                break;
            case WM_MOUSEMOVE:
                if (g_held) {
                    // Cursor stays put because we block the move; pt - anchor is this event's delta.
                    g_hookDx += m->pt.x - g_anchor.x;
                    g_hookDy += m->pt.y - g_anchor.y;
                    return 1;
                }
                break;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void OnRawInput(HRAWINPUT handle) {
    RAWINPUT ri;
    UINT size = sizeof(ri);
    if (GetRawInputData(handle, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) return;
    if (ri.header.dwType != RIM_TYPEMOUSE) return;
    const RAWMOUSE& mo = ri.data.mouse;
    if (g_held && !(mo.usFlags & MOUSE_MOVE_ABSOLUTE) && (mo.lLastX || mo.lLastY)) {
        g_rawDx += mo.lLastX;
        g_rawDy += mo.lLastY;
        g_rawSeen = true;
    }
    // Physical button state also arrives here, even if the hook missed the button-up
    // (e.g. while a UAC prompt had the secure desktop) - never leave fingers stuck down.
    if (mo.usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP) SignalRelease();
}

void InjectMiddleClick() {
    INPUT in[2]{};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
    in[0].mi.dwExtraInfo = kInjectTag;
    in[1] = in[0];
    in[1].mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
    SendInput(2, in, sizeof(INPUT));
}

// ---------------------------------------------------------------------------
// Tray UI
// ---------------------------------------------------------------------------
void UpdateTray(DWORD message) {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = g_hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    const wchar_t* state = !g_deviceReady ? L"virtual touchpad not found"
                         : g_enabled      ? L"active"
                                          : L"disabled";
    swprintf_s(nid.szTip, L"MiddlePtp - %s", state);
    Shell_NotifyIconW(message, &nid);
}

void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    HMENU sens = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_STATUS,
                g_deviceReady ? L"Virtual touchpad: connected" : L"Virtual touchpad: NOT FOUND");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_enabled ? MF_CHECKED : 0), ID_ENABLED, L"Enabled");
    AppendMenuW(menu, MF_STRING | (g_invert ? MF_CHECKED : 0), ID_INVERT, L"Invert direction");
    AppendMenuW(menu, MF_STRING | (g_axisLock ? MF_CHECKED : 0), ID_AXISLOCK, L"Lock to one axis");
    for (int i = 0; i < (int)ARRAYSIZE(kSensPresets); ++i) {
        wchar_t label[32];
        swprintf_s(label, L"%gx", kSensPresets[i]);
        const bool checked = std::abs(g_sensitivity - kSensPresets[i]) < 1e-6;
        AppendMenuW(sens, MF_STRING | (checked ? MF_CHECKED : 0), ID_SENS_BASE + i, label);
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)sens, L"Sensitivity");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EDIT_INI, L"Edit settings file...");
    AppendMenuW(menu, MF_STRING, ID_RELOAD, L"Reload settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);   // also destroys the submenu
}

void OnCommand(UINT id) {
    switch (id) {
    case ID_ENABLED:  g_enabled = !g_enabled;   break;
    case ID_INVERT:   g_invert = !g_invert;     break;
    case ID_AXISLOCK: g_axisLock = !g_axisLock; break;
    case ID_EDIT_INI: ShellExecuteW(nullptr, L"open", g_iniPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return;
    case ID_RELOAD:   LoadSettings(); UpdateTray(NIM_MODIFY); return;
    case ID_EXIT:     DestroyWindow(g_hwnd); return;
    default:
        if (id >= ID_SENS_BASE && id < ID_SENS_BASE + ARRAYSIZE(kSensPresets))
            g_sensitivity = kSensPresets[id - ID_SENS_BASE];
        else
            return;
    }
    SaveSettings();
    UpdateTray(NIM_MODIFY);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INPUT:
        OnRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        break;   // DefWindowProc must still run for WM_INPUT
    case WM_APP_CLICK:
        InjectMiddleClick();
        return 0;
    case WM_APP_STATUS:
        UpdateTray(NIM_MODIFY);
        return 0;
    case WM_APP_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) ShowMenu();
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) OnCommand(ID_ENABLED);
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;
    case WM_DESTROY:
        UpdateTray(NIM_DELETE);
        PostQuitMessage(0);
        return 0;
    default:
        if (msg == g_taskbarCreated && msg != 0) { UpdateTray(NIM_ADD); return 0; }   // explorer restarted
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MiddlePtp.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, ARRAYSIZE(exePath));
    g_iniPath = exePath;
    g_iniPath = g_iniPath.substr(0, g_iniPath.find_last_of(L'.')) + L".ini";
    LoadSettings();

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"MiddlePtpWindow";
    RegisterClassW(&wc);
    // Hidden top-level window (not message-only, so it receives TaskbarCreated).
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"MiddlePtp", WS_OVERLAPPED, 0, 0, 0, 0,
                             nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) return 1;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    RAWINPUTDEVICE rid{0x01, 0x02, RIDEV_INPUTSINK, g_hwnd};   // generic desktop / mouse
    RegisterRawInputDevices(&rid, 1, sizeof(rid));

    g_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_quitEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE worker = CreateThread(nullptr, 0, WorkerMain, nullptr, 0, nullptr);

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, instance, 0);
    UpdateTray(NIM_ADD);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hook) UnhookWindowsHookEx(g_hook);
    g_quit = true;
    SetEvent(g_quitEvent);
    if (worker) { WaitForSingleObject(worker, 2000); CloseHandle(worker); }
    CloseHandle(g_wakeEvent);
    CloseHandle(g_quitEvent);
    if (mutex) CloseHandle(mutex);
    return 0;
}
