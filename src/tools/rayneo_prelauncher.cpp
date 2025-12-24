#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <iostream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <atomic>

// RayNeo SDK
#include "rayneo_api.h"

static std::wstring ReadRegString(HKEY root, const wchar_t* subkey, const wchar_t* value, REGSAM view)
{
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(root, subkey, 0, KEY_READ | view, &hKey);
    if (rc != ERROR_SUCCESS) return L"";
    wchar_t buf[1024]; DWORD type = 0; DWORD size = sizeof(buf);
    rc = RegQueryValueExW(hKey, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS || type != REG_SZ) return L"";
    return std::wstring(buf);
}

static bool LaunchSteamVR()
{
    HINSTANCE h = ShellExecuteW(nullptr, L"open", L"steam://rungameid/250820", nullptr, nullptr, SW_SHOWDEFAULT);
    return reinterpret_cast<INT_PTR>(h) > 32;
}

int wmain()
{
    // 1) Connect to RayNeo and set 3D mode
    RAYNEO_Context ctx = nullptr;
    if (Rayneo_Create(&ctx) != RAYNEO_OK || !ctx) {
        std::wcerr << L"Rayneo_Create failed" << std::endl;
        return 1;
    }
    const uint16_t kVid = 0x1BBB;
    const uint16_t kPid = 0xAF50;
    Rayneo_SetTargetVidPid(ctx, kVid, kPid);
    

    if (Rayneo_Start(ctx, 0) != RAYNEO_OK) {
        std::wcerr << L"Rayneo_Start failed (device not found)" << std::endl;
        Rayneo_Destroy(ctx);
        return 2;
    }
    std::cout << "RayNeo device started" << std::endl;

    RAYNEO_Result r3d = Rayneo_DisplaySet3D(ctx);
    if (r3d != RAYNEO_OK) {
        std::wcerr << L"Rayneo_DisplaySet3D failed: " << (int)r3d << std::endl;
    }

    std::cout << "RayNeo set to 3D mode" << std::endl;

    // Run event loop in a separate thread to process acks and device info
    std::atomic<bool> loopRun{true};
    std::atomic<bool> sawInfo{false};
    std::thread evtThread([&]{
        while (loopRun.load()) {
            RAYNEO_Event evt{};
            auto rc = Rayneo_PollEvent(ctx, &evt, 500);
            if (rc == RAYNEO_OK) {
                if (evt.type == RAYNEO_EVENT_DEVICE_INFO) {
                    // sawInfo.store(true);
                    std::cout << "RayNeo device info received" << std::endl;
                } else if (evt.type == RAYNEO_EVENT_NOTIFY) {
                    std::cout << "RayNeo notify code=" << (int)evt.data.notify.code << std::endl;
                } else if (evt.type == RAYNEO_EVENT_LOG) {
                    std::cout << "RayNeo log: " << evt.data.log.message << std::endl;
                }
            }
        }
    });

    // Wait up to 3 seconds or until device info is seen
    {
        auto start = std::chrono::steady_clock::now();
        auto duration = std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() - start < duration && !sawInfo.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Small grace wait to let Windows re-enumerate the display
    // std::this_thread::sleep_for(std::chrono::milliseconds(800));

    // Stop event loop thread cleanly
    loopRun.store(false);
    evtThread.join();

    // Disconnect/cleanup
    std::cout << "Stopping RayNeo device..." << std::endl;
    Rayneo_Stop(ctx);
    // std::cout << "RayNeo device destroyed   " << std::endl;
    // Rayneo_Destroy(ctx);
    std::cout << "RayNeo device stopped" << std::endl;
    // return 0;
    std::cout << "Launching SteamVR..." << std::endl;
    // 2) Launch SteamVR
    if (!LaunchSteamVR()) {
        std::wcerr << L"Failed to launch SteamVR" << std::endl;
        return 3;
    }
    return 0;
}
#else
int main() { return 0; }
#endif
