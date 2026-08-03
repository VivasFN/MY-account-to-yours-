// SteamBridgeLauncher.cpp
// x86 launcher for Wolf RPG Editor games (Steam achievements bridge)
// Build target: Windows 32-bit (Release)

#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "steam/steam_api.h"

namespace fs = std::filesystem;

static const wchar_t* kGameExe = L"Game.exe"; // WOLF game executable
static const wchar_t* kCmdDir  = L"steam_cmd"; // command directory from game side

void Log(const std::string& msg) {
    std::ofstream ofs("steam_bridge.log", std::ios::app);
    ofs << msg << "\n";
}

bool UnlockAchievement(const std::string& id) {
    if (!SteamUserStats()) return false;

    bool ok = SteamUserStats()->SetAchievement(id.c_str());
    if (!ok) {
        Log("SetAchievement failed: " + id);
        return false;
    }

    ok = SteamUserStats()->StoreStats();
    if (!ok) {
        Log("StoreStats failed after SetAchievement: " + id);
        return false;
    }

    Log("Achievement unlocked: " + id);
    return true;
}

std::vector<fs::path> CollectCommandFiles(const fs::path& dir) {
    std::vector<fs::path> files;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return files;

    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.is_regular_file()) files.push_back(e.path());
    }
    return files;
}

void ProcessCommandFile(const fs::path& p) {
    // Expected command examples:
    // unlock ACH_TEST_01
    // unlock ACH_CLEAR_STAGE_1

    std::ifstream ifs(p);
    if (!ifs) {
        Log("Failed to open cmd file: " + p.string());
        return;
    }

    std::string cmd;
    std::string arg;
    ifs >> cmd >> arg;

    if (cmd == "unlock" && !arg.empty()) {
        UnlockAchievement(arg);
    } else {
        Log("Unknown command in " + p.string());
    }

    // remove after processing
    std::error_code ec;
    fs::remove(p, ec);
    if (ec) Log("Failed to delete cmd file: " + p.string());
}

PROCESS_INFORMATION LaunchGame() {
    STARTUPINFOW si{};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi{};
    std::wstring cmd = std::wstring(L"\"") + kGameExe + L"\"";

    BOOL ok = CreateProcessW(
        nullptr,
        cmd.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!ok) {
        DWORD err = GetLastError();
        Log("CreateProcessW failed. error=" + std::to_string(err));
        throw std::runtime_error("Failed to launch Game.exe");
    }

    Log("Game launched.");
    return pi;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    Log("=== SteamBridgeLauncher start ===");

    if (!SteamAPI_Init()) {
        MessageBoxW(nullptr, L"SteamAPI_Init failed. Launch from Steam client.", L"Steam Bridge", MB_ICONERROR);
        Log("SteamAPI_Init failed");
        return 1;
    }

    Log("SteamAPI_Init ok");

    PROCESS_INFORMATION pi{};
    try {
        pi = LaunchGame();
    } catch (const std::exception& ex) {
        MessageBoxA(nullptr, ex.what(), "Steam Bridge", MB_ICONERROR);
        SteamAPI_Shutdown();
        return 1;
    }

    fs::create_directories(kCmdDir);
    fs::path cmdDir(kCmdDir);

    // poll loop while game process is alive
    bool running = true;
    while (running) {
        SteamAPI_RunCallbacks();

        DWORD waitResult = WaitForSingleObject(pi.hProcess, 50);
        if (waitResult == WAIT_OBJECT_0) {
            running = false;
        }

        auto files = CollectCommandFiles(cmdDir);
        for (const auto& f : files) {
            ProcessCommandFile(f);
        }

        Sleep(100);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    SteamAPI_Shutdown();
    Log("=== SteamBridgeLauncher end ===");
    return 0;
}
