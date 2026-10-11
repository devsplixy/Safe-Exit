#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct TargetInfo {
    std::vector<DWORD> pids;
    std::string execName;
    std::string absolutePath;
    int revision;
};

std::string FormatGameName(const std::string& exeName, int revision);
std::string GetBase64Icon(const std::string& exePath);
TargetInfo ScoutTarget(const std::string& manualExeName, const std::string& manualExePath);
std::vector<std::string> HarvestSockets(const std::vector<DWORD>& pids);