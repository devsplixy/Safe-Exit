#include "Watchdog.h"
#include "Engine.h"
#include <string>

DWORD WINAPI WatchdogMonitorThread(LPVOID lpParam) {
    HANDLE hWatchdog = (HANDLE)lpParam;
    WaitForSingleObject(hWatchdog, INFINITE);
    RemoveLockdownRules();
    ExitProcess(0);
    return 0;
}

void DeployWatcher() {
    char exeBuffer[MAX_PATH];
    GetModuleFileNameA(NULL, exeBuffer, MAX_PATH);
    std::string args = std::string(exeBuffer) + " --shadow " + std::to_string(GetCurrentProcessId());

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, (LPSTR)args.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CreateThread(NULL, 0, WatchdogMonitorThread, (LPVOID)pi.hProcess, 0, NULL);
    }
}

void TheWatcher(DWORD hostPid) {
    HANDLE hHost = OpenProcess(SYNCHRONIZE, FALSE, hostPid);
    if (hHost) {
        WaitForSingleObject(hHost, INFINITE);
        CloseHandle(hHost);
    }
    CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    RemoveLockdownRules();
    CoUninitialize();
    ExitProcess(0);
}