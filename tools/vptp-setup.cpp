// vptp-setup.exe - creates / updates / removes the root-enumerated Root\VirtualPtp device.
// (The bits of devcon.exe we need, so users don't have to install the WDK.)
//
//   vptp-setup install <path\to\VirtualPtp.inf>   create the device if missing, then install/update the driver
//   vptp-setup remove                               remove every Root\VirtualPtp device
//   vptp-setup status                               print how many devices exist
//
// Exit codes: 0 = ok, 1 = ok but reboot required, 2 = error.
// Build: cl /nologo /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE vptp-setup.cpp /link setupapi.lib newdev.lib

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cstdio>
#include <cwchar>
#include <vector>

#ifndef MAX_CLASS_NAME_LEN
#define MAX_CLASS_NAME_LEN 32   // setupapi.h value (missing in some non-MSVC headers)
#endif

namespace {

const wchar_t kHwid[] = L"Root\\VirtualPtp";
const wchar_t kHwidMultiSz[] = L"Root\\VirtualPtp\0";   // literal adds the second terminating NUL

int Fail(const wchar_t* what) {
    const DWORD err = GetLastError();
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    fwprintf(stderr, L"%s failed: 0x%08lX %s\n", what, err, msg ? msg : L"");
    if (msg) LocalFree(msg);
    return 2;
}

bool HasOurHwid(HDEVINFO set, SP_DEVINFO_DATA* dev) {
    DWORD type = 0, size = 0;
    SetupDiGetDeviceRegistryPropertyW(set, dev, SPDRP_HARDWAREID, &type, nullptr, 0, &size);
    if (size == 0) return false;
    std::vector<BYTE> buf(size + 2 * sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, dev, SPDRP_HARDWAREID, &type, buf.data(), size, nullptr)) return false;
    for (auto* p = reinterpret_cast<const wchar_t*>(buf.data()); *p; p += wcslen(p) + 1)
        if (_wcsicmp(p, kHwid) == 0) return true;
    return false;
}

// Calls fn(set, dev) for every device (present or not) carrying our hardware id.
template <class Fn>
int ForEachOurDevice(Fn fn) {
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE) return -1;
    SP_DEVINFO_DATA dev{};
    dev.cbSize = sizeof(dev);
    int count = 0;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dev); ++i) {
        if (HasOurHwid(set, &dev)) { ++count; fn(set, &dev); }
    }
    SetupDiDestroyDeviceInfoList(set);
    return count;
}

int Install(const wchar_t* infArg) {
    wchar_t inf[MAX_PATH];
    if (!GetFullPathNameW(infArg, MAX_PATH, inf, nullptr)) return Fail(L"GetFullPathName");
    if (GetFileAttributesW(inf) == INVALID_FILE_ATTRIBUTES) return Fail(L"Opening INF");

    if (ForEachOurDevice([](HDEVINFO, SP_DEVINFO_DATA*) {}) <= 0) {
        GUID classGuid;
        wchar_t className[MAX_CLASS_NAME_LEN];
        if (!SetupDiGetINFClassW(inf, &classGuid, className, MAX_CLASS_NAME_LEN, nullptr))
            return Fail(L"SetupDiGetINFClass");

        HDEVINFO set = SetupDiCreateDeviceInfoList(&classGuid, nullptr);
        if (set == INVALID_HANDLE_VALUE) return Fail(L"SetupDiCreateDeviceInfoList");
        SP_DEVINFO_DATA dev{};
        dev.cbSize = sizeof(dev);
        int rc = 0;
        if (!SetupDiCreateDeviceInfoW(set, className, &classGuid, nullptr, nullptr, DICD_GENERATE_ID, &dev))
            rc = Fail(L"SetupDiCreateDeviceInfo");
        else if (!SetupDiSetDeviceRegistryPropertyW(set, &dev, SPDRP_HARDWAREID,
                                                    reinterpret_cast<const BYTE*>(kHwidMultiSz), sizeof(kHwidMultiSz)))
            rc = Fail(L"SetupDiSetDeviceRegistryProperty");
        else if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &dev))
            rc = Fail(L"SetupDiCallClassInstaller(DIF_REGISTERDEVICE)");
        SetupDiDestroyDeviceInfoList(set);
        if (rc) return rc;
        wprintf(L"Created device %s\n", kHwid);
    }

    BOOL reboot = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, kHwid, inf, INSTALLFLAG_FORCE, &reboot))
        return Fail(L"UpdateDriverForPlugAndPlayDevices");
    wprintf(L"Driver installed%s\n", reboot ? L" (reboot required)" : L"");
    return reboot ? 1 : 0;
}

int Remove() {
    bool reboot = false, failed = false;
    const int n = ForEachOurDevice([&](HDEVINFO set, SP_DEVINFO_DATA* dev) {
        // Same as `devcon remove`: DIF_REMOVE via the class installer.
        if (!SetupDiCallClassInstaller(DIF_REMOVE, set, dev)) { Fail(L"SetupDiCallClassInstaller(DIF_REMOVE)"); failed = true; return; }
        SP_DEVINSTALL_PARAMS_W params{};
        params.cbSize = sizeof(params);
        if (SetupDiGetDeviceInstallParamsW(set, dev, &params) && (params.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART)))
            reboot = true;
    });
    if (n < 0) return Fail(L"SetupDiGetClassDevs");
    wprintf(L"Removed %d device(s)\n", failed ? 0 : n);
    return failed ? 2 : (reboot ? 1 : 0);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 3 && _wcsicmp(argv[1], L"install") == 0) return Install(argv[2]);
    if (argc >= 2 && _wcsicmp(argv[1], L"remove") == 0)  return Remove();
    if (argc >= 2 && _wcsicmp(argv[1], L"status") == 0) {
        const int n = ForEachOurDevice([](HDEVINFO, SP_DEVINFO_DATA*) {});
        wprintf(L"%d %s device(s)\n", n < 0 ? 0 : n, kHwid);
        return 0;
    }
    fwprintf(stderr, L"usage: vptp-setup install <VirtualPtp.inf> | remove | status\n");
    return 2;
}
