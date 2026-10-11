#include "SystemUtils.h"
#include <wininet.h>
#include <shellapi.h>
#include <netfw.h>

using namespace std;

bool IsUserAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

void ElevateAndExit() {
    wchar_t szPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, szPath, ARRAYSIZE(szPath))) {
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = szPath;
        sei.hwnd = NULL;
        sei.nShow = SW_NORMAL;
        if (ShellExecuteExW(&sei)) ExitProcess(0);
    }
}

bool VerifyConnectivity() {
    DWORD flags = 0;
    return InternetGetConnectedState(&flags, 0) == TRUE;
}

bool EnsureFirewallEnabled() {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"HNetCfg.FwPolicy2", &clsid))) return false;
    INetFwPolicy2* policy = NULL;
    if (FAILED(CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2), (void**)&policy)) || !policy) return false;

    NET_FW_PROFILE_TYPE2 profiles[3] = { NET_FW_PROFILE2_DOMAIN, NET_FW_PROFILE2_PRIVATE, NET_FW_PROFILE2_PUBLIC };
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
        VARIANT_BOOL on = VARIANT_FALSE;
        if (SUCCEEDED(policy->get_FirewallEnabled(profiles[i], &on)) && on == VARIANT_FALSE) {
            if (FAILED(policy->put_FirewallEnabled(profiles[i], VARIANT_TRUE))) { ok = false; continue; }
            VARIANT_BOOL nowOn = VARIANT_FALSE;
            policy->get_FirewallEnabled(profiles[i], &nowOn);
            if (nowOn == VARIANT_FALSE) ok = false;
        }
    }
    policy->Release();
    return ok;
}

uint32_t IpToUint(const string& ipStr) {
    return ntohl(inet_addr(ipStr.c_str()));
}

string UintToIp(uint32_t ip) {
    struct in_addr addr;
    addr.S_un.S_addr = htonl(ip);
    return inet_ntoa(addr);
}

string FormatRange(uint32_t start, uint32_t end) {
    return (start == end) ? UintToIp(start) : UintToIp(start) + "-" + UintToIp(end);
}