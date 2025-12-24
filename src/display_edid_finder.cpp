#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#pragma comment(lib, "setupapi.lib")
#endif

#if defined(__unix__) && !defined(__APPLE__)
// X11 + RandR for monitor geometry (best effort). Linking expectations: X11 and Xrandr libraries available.
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#endif

#include "display_edid_finder.h"
#include <array>
#include <cstring>
#include <sstream>
#include <thread>
#include <chrono>

namespace {

// Common EDID parsing utilities (used on all platforms that retrieve EDID bytes)
uint16_t DecodeManufacturerId(uint16_t raw) {
    uint16_t be = (raw >> 8) | ((raw & 0xFF) << 8);
    char c1 = ((be >> 10) & 0x1F) + 64; (void)c1;
    char c2 = ((be >> 5) & 0x1F) + 64; (void)c2;
    char c3 = (be & 0x1F) + 64; (void)c3;
    return be;
}

std::string ExtractMonitorName(const uint8_t* edid, size_t len) {
    if (len < 128) return {};
    for (size_t i = 54; i + 18 <= 126; i += 18) {
        const uint8_t* block = edid + i;
        if (block[0] == 0x00 && block[1] == 0x00 && block[2] == 0x00 && block[3] == 0xFC) {
            char name[14] = {0};
            size_t copyLen = 13;
            for (size_t j = 5, k = 0; j < 18 && k < copyLen; ++j) {
                char c = static_cast<char>(block[j]);
                if (c == '\n' || c == '\r') break;
                name[k++] = c;
            }
            std::string s(name);
            while (!s.empty() && s.back() == ' ') s.pop_back();
            return s;
        }
    }
    return {};
}

DisplayEdidInfo ParseEdid(const std::string& instanceId, const std::vector<uint8_t>& edid) {
    DisplayEdidInfo info; info.device_instance_id = instanceId;
    if (edid.size() >= 128) {
        info.manufacturer_id = DecodeManufacturerId(static_cast<uint16_t>((edid[8] << 8) | edid[9]));
        info.product_code = static_cast<uint16_t>(edid[10] | (edid[11] << 8));
        info.serial_number = (edid[12]) | (edid[13] << 8) | (edid[14] << 16) | (edid[15] << 24);
        info.week_of_manufacture = edid[16];
        info.year_of_manufacture = 1990 + edid[17];
        info.monitor_name = ExtractMonitorName(edid.data(), edid.size());

        if (edid.size() >= 72) {
            const uint8_t* dt = &edid[54];
            bool is_detailed_timing = !(dt[0] == 0x00 && dt[1] == 0x00);
            if (is_detailed_timing) {
                uint16_t h_active = static_cast<uint16_t>(dt[2] | ((dt[4] & 0xF0) << 4));
                uint16_t v_active = static_cast<uint16_t>(dt[5] | ((dt[7] & 0xF0) << 4));
                info.preferred_width = h_active;
                info.preferred_height = v_active;
            }
        }
    }
    return info;
}

#ifdef _WIN32

bool ReadRegistryBinary(HKEY hKey, const char* valueName, std::vector<uint8_t>& out) {
    DWORD type = 0; DWORD size = 0;
    if (RegQueryValueExA(hKey, valueName, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_BINARY || size == 0) return false;
    out.resize(size);
    if (RegQueryValueExA(hKey, valueName, nullptr, nullptr, out.data(), &size) != ERROR_SUCCESS) { out.clear(); return false; }
    return true;
}

std::vector<DisplayEdidInfo> EnumerateEdidsWindows() {
    std::vector<DisplayEdidInfo> result;
    HDEVINFO devInfo = SetupDiGetClassDevsExA(&GUID_DEVCLASS_MONITOR, "DISPLAY", nullptr, DIGCF_PRESENT, nullptr, nullptr, nullptr);
    if (devInfo == INVALID_HANDLE_VALUE) return result;
    for (DWORD i = 0; ; ++i) {
        SP_DEVINFO_DATA devData{}; devData.cbSize = sizeof(devData);
        if (!SetupDiEnumDeviceInfo(devInfo, i, &devData)) break;
        char instanceId[256];
        if (!SetupDiGetDeviceInstanceIdA(devInfo, &devData, instanceId, sizeof(instanceId), nullptr)) continue;
        HKEY hDevRegKey = SetupDiOpenDevRegKey(devInfo, &devData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hDevRegKey == INVALID_HANDLE_VALUE) continue;
        std::vector<uint8_t> edid;
        if (ReadRegistryBinary(hDevRegKey, "EDID", edid)) {
            result.push_back(ParseEdid(instanceId, edid));
        }
        RegCloseKey(hDevRegKey);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

#endif // _WIN32

} // namespace

std::vector<DisplayEdidInfo> DisplayEdidFinder::EnumerateAll() {
#ifdef _WIN32
    return EnumerateEdidsWindows();
#elif defined(__unix__) && !defined(__APPLE__)
    std::vector<DisplayEdidInfo> result;
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return result;
    Window root = DefaultRootWindow(dpy);
    XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
    if (!res) { XCloseDisplay(dpy); return result; }

    Atom edidAtom = XInternAtom(dpy, "EDID", True);
    if (edidAtom == None) {
        // Some systems use EDID or XFree86_DDC_EDID
        edidAtom = XInternAtom(dpy, "XFREE86_DDC_EDID", True);
    }

    for (int i = 0; i < res->noutput; ++i) {
        RROutput output = res->outputs[i];
        XRROutputInfo *outInfo = XRRGetOutputInfo(dpy, res, output);
        if (!outInfo) continue;
        if (outInfo->connection == RR_Connected && outInfo->crtc) {
            // Try EDID property
            if (edidAtom != None) {
                Atom actualType; int actualFormat; unsigned long nItems; unsigned long bytesAfter; unsigned char *prop = nullptr;
                int status = XRRGetOutputProperty(dpy, output, edidAtom, 0, 128, False, False, AnyPropertyType,
                                                  &actualType, &actualFormat, &nItems, &bytesAfter, &prop);
                if (status == Success && prop && nItems >= 128) {
                    std::vector<uint8_t> edid(prop, prop + nItems);
                    // Build synthetic instance id from output name
                    std::string instanceId = std::string("X11:") + (outInfo->name ? outInfo->name : "output");
                    DisplayEdidInfo info = ParseEdid(instanceId, edid);
                    // Desktop geometry via CRTC
                    XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, res, outInfo->crtc);
                    if (crtc) {
                        info.desktop_x = crtc->x;
                        info.desktop_y = crtc->y;
                        info.desktop_width = crtc->width;
                        info.desktop_height = crtc->height;
                        XRRFreeCrtcInfo(crtc);
                    }
                    result.push_back(info);
                }
                if (prop) XFree(prop);
            }
        }
        XRRFreeOutputInfo(outInfo);
    }
    XRRFreeScreenResources(res);
    XCloseDisplay(dpy);
    return result;
#else
    return {};
#endif
}

std::optional<DisplayEdidInfo> DisplayEdidFinder::FindDisplayByEdid(uint16_t product_code_filter, std::optional<uint32_t> serial_number_filter) {
    auto all = EnumerateAll();
    for (const auto& d : all) {
        if (d.product_code == product_code_filter) {
            if (!serial_number_filter || d.serial_number == *serial_number_filter) {
                return d;
            }
        }
    }
    return std::nullopt;
}

bool DisplayEdidFinder::PopulateDesktopCoordinates(DisplayEdidInfo &info) {
#ifdef _WIN32
    // We match via EnumDisplayDevices adapter->monitor enumeration.
    // The info.device_instance_id looks like: DISPLAY\TCL03D4\7&26951BDF&0&UID268
    // The monitor.DeviceID looks like: MONITOR\TCL03D4\{GUID}\0001
    // We extract the model part (e.g. TCL03D4) and match on that.
    
    // Extract model from instance ID (second segment after DISPLAY\)
    std::string instanceModel;
    {
        std::string inst = info.device_instance_id;
        size_t first = inst.find('\\');
        if (first != std::string::npos) {
            size_t second = inst.find('\\', first + 1);
            if (second != std::string::npos) {
                instanceModel = inst.substr(first + 1, second - first - 1);
            }
        }
    }
    
    // Wait up to 5 seconds for Windows to recognize the display after mode switch
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    
    while (std::chrono::steady_clock::now() < deadline) {
        DISPLAY_DEVICEA adapter{}; adapter.cb = sizeof(adapter);
        for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &adapter, 0); ++i) {
            // Check if adapter is active
            if (!(adapter.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
            
            DISPLAY_DEVICEA monitor{}; monitor.cb = sizeof(monitor);
            for (DWORD j = 0; EnumDisplayDevicesA(adapter.DeviceName, j, &monitor, 0); ++j) {
                if (monitor.DeviceID[0] == '\0') continue;
                
                std::string devId = monitor.DeviceID;
                
                // Extract model from DeviceID (e.g. MONITOR\TCL03D4\...)
                std::string monitorModel;
                {
                    size_t first = devId.find('\\');
                    if (first != std::string::npos) {
                        size_t second = devId.find('\\', first + 1);
                        if (second != std::string::npos) {
                            monitorModel = devId.substr(first + 1, second - first - 1);
                        }
                    }
                }
                
                // Case-insensitive compare of model parts
                std::string instModelLower = instanceModel;
                std::string monModelLower = monitorModel;
                for (auto &c : instModelLower) c = (char)tolower((unsigned char)c);
                for (auto &c : monModelLower) c = (char)tolower((unsigned char)c);
                
                if (!instModelLower.empty() && instModelLower == monModelLower) {
                    // Use adapter.DeviceName (e.g. \\\\.\\DISPLAY3) to get current settings
                    DEVMODEA dm{}; dm.dmSize = sizeof(dm);
                    if (EnumDisplaySettingsExA(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0)) {
                        info.desktop_x = dm.dmPosition.x;
                        info.desktop_y = dm.dmPosition.y;
                        info.desktop_width = dm.dmPelsWidth;
                        info.desktop_height = dm.dmPelsHeight;
                        return true;
                    }
                }
            }
        }
        // Not found yet, wait a bit and retry
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    
    return false;
// ---------------- Linux (X11) implementation ----------------
#elif defined(__unix__) && !defined(__APPLE__)
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) { return false; }
    Window root = DefaultRootWindow(dpy);
    int numMonitors = 0;
    XRRMonitorInfo *monitors = XRRGetMonitors(dpy, root, true, &numMonitors);
    bool found = false;
    if (monitors) {
        // We cannot directly match EDID product_code to XRRMonitorInfo easily without reading EDID via XRR.
        // Simplify: choose primary (first) monitor if product_code matches any EDID atoms (future improvement).
        // For now, pick the first monitor as heuristic when resolution matches preferred_width/height.
        for (int i = 0; i < numMonitors; ++i) {
            const XRRMonitorInfo &m = monitors[i];
            // Use preferred_width/height hint if set to choose matching monitor.
            if (info.preferred_width && info.preferred_height) {
                if ((int)info.preferred_width == m.width && (int)info.preferred_height == m.height) {
                    info.desktop_x = m.x; info.desktop_y = m.y; info.desktop_width = m.width; info.desktop_height = m.height; found = true; break;
                }
            }
        }
        if (!found && numMonitors > 0) {
            // fallback: first monitor
            info.desktop_x = monitors[0].x;
            info.desktop_y = monitors[0].y;
            info.desktop_width = monitors[0].width;
            info.desktop_height = monitors[0].height;
            found = true;
        }
        XRRFreeMonitors(monitors);
    }
    XCloseDisplay(dpy);
    return found;
#else
    (void)info; return false;
#endif
}
