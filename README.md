# VirtualPtp + MiddlePtp

Hold the **middle mouse button and move the mouse** → Windows sees a **two‑finger scroll on a real Precision Touchpad**. Windows' own touchpad engine then does the scrolling, so you get native smooth scrolling with inertia everywhere, including WinUI/UWP apps.

```
mouse ──► MiddlePtp.exe (tray app) ──► VirtualPtp driver (UMDF2) ──► Windows touchpad engine ──► app
           hook + raw input              virtual Precision Touchpad       (DirectManipulation,
           builds 2-finger frames                                           inertia, gestures)
```

* **No test mode.** It's a user-mode driver signed with a certificate that only your PC trusts. Secure Boot stays on, and anti-cheat doesn't care.
* **Requires Windows 11** (build 22000+), x64.

## Install (from a GitHub build)

1. Download **VirtualPtp-x64** from the latest run on the **Actions** tab, or `VirtualPtp-x64.zip` from **Releases**, and extract it.
2. Open **PowerShell as Administrator** in the extracted `VirtualPtp` folder and run:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install.ps1
   ```
   This creates a local signing certificate (once), signs the driver with it, and installs the **Virtual Precision Touchpad** device.
3. Start **`MiddlePtp.exe`**. To run it at login, put a shortcut to it in `shell:startup`.
4. Change the touchpad settings in the next section.

To remove everything: `powershell -ExecutionPolicy Bypass -File .\uninstall.ps1 -RemoveCertificate`

> **About the certificate:** the private key stays on this PC. Anything able to read it, which needs admin, could sign code your PC trusts. Run `install.ps1 -ForgetSigningKey` to delete the key right after signing. Re-installing then just creates a new one.

## Windows touchpad settings

These are in **Settings → Bluetooth & devices → Touchpad**. The page appears once the driver is installed, even on a desktop.

| Setting | Set to | Why |
|---|---|---|
| Touchpad | On | |
| **Leave touchpad on when a mouse is connected** | **On** | otherwise Windows ignores the virtual pad entirely |
| Taps → **Touchpad sensitivity** | **Most sensitive** | otherwise scrolls right after typing get suppressed as "palm" |
| Taps → Tap with two fingers to right-click | Off (recommended) | extra protection against accidental right-clicks |
| Scroll & zoom → Drag two fingers to scroll | On | |
| Scroll & zoom → Scrolling direction | your taste | Can also be flipped with the tray option *Invert direction* |

## Using it

* **Hold middle + move** → scroll in any direction. Let go mid-motion → inertia.
* **Quick middle click without moving** → still a normal middle click.
* **Tray icon:** right-click for Enabled, Invert, Axis lock, Sensitivity presets and settings. Double-click toggles on/off.
* **`MiddlePtp.ini`** (created next to the exe):

| Key | Default | Meaning |
|---|---|---|
| `Sensitivity` | `1.0` | finger travel (1/1000 inch) per mouse count |
| `Invert` | `0` | flip direction |
| `AxisLock` | `0` | lock each gesture to its initial dominant axis |
| `DeadzoneCounts` | `6` | movement before fingers touch down (keeps clicks as clicks) |
| `ClickTimeoutMs` | `400` | press+release without movement within this → middle click |
| `MinContactMs` | `250` | very short, tiny gestures are held this long so Windows never sees a two-finger tap |
| `FrameIntervalMs` | `8` | frame rate (8 ms ≈ 125 Hz, like real touchpads) |
| `FingerSpacing` | `700` | distance between the virtual fingers (~18 mm) |
| `ExcludedApps` | `blender.exe` | comma-separated exe names where middle-drag is passed through untouched |

## Repository layout / building

| Path | What |
|---|---|
| `driver/` | UMDF2 HID minidriver: Precision Touchpad descriptor, IOCTL handling, INF, `VirtualPtp.vcxproj` |
| `app/MiddlePtp.cpp` | tray app (mouse hook + raw input → two-finger frames) |
| `tools/vptp-setup.cpp` | tiny SetupAPI tool that creates/removes the root device (replaces devcon) |
| `scripts/` | install / uninstall |
| `.github/workflows/build.yml` | CI build |

**GitHub Actions** builds everything on every push. It pulls the Windows SDK and WDK from NuGet (`packages.config` + `Directory.Build.props`), so nothing needs to be installed. Push a tag like `v0.1.0` to also publish a Release zip.

**Local build** (optional, needs VS 2022/2026 with C++):
```powershell
nuget restore .\packages.config -PackagesDirectory .\packages\
msbuild driver\VirtualPtp.vcxproj -p:Configuration=Release -p:Platform=x64
```

## Known limitations

* **Very long continuous drags:** when the virtual fingers reach the edge of the virtual 16 × 10 cm pad, the app lifts and re-places them. That's roughly every 8 cm of vertical finger travel. You may notice a small hitch there. Lower the sensitivity if it happens often.
* **Elevated windows:** a non-elevated MiddlePtp can't capture input over admin windows. Run it elevated if you need that.
* **Excluded apps:** apps that use middle-drag themselves (Blender, CAD, some games) need to go in `ExcludedApps`.

## Troubleshooting

* **Tray says "virtual touchpad not found"**: check Device Manager for a yellow bang on *Virtual Precision Touchpad*. Also check `Event Viewer → Applications and Services Logs → Microsoft → Windows → DriverFrameworks-UserMode`.
* **Device is fine but nothing scrolls**: check *Leave touchpad on when a mouse is connected* first.
* **CI build failed**: the run uploads `build-logs` (an MSBuild binlog). Open it with the MSBuild Structured Log Viewer.
