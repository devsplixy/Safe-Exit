#include "GameScanner.h"
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <gdiplus.h>
#include <wincrypt.h>
#include <regex>

using namespace std;

static string WideToAcp(const wchar_t* w) {
    if (!w || !*w) return "";
    int len = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return "";
    string s(len - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, w, -1, &s[0], len, NULL, NULL);
    return s;
}

static bool GetFcRevision(const string& nameNoExt, int& revision) {
    string norm;
    for (char c : nameNoExt) {
        if (c == ' ' || c == '_' || c == '-' || c == '.') continue;
        norm += (char)tolower((unsigned char)c);
    }
    smatch m;
    if (regex_search(norm, m, regex("fc(\\d{2,})"))) { revision = stoi(m[1].str()); return true; }
    return false;
}

string FormatGameName(const string& exeName, int revision) {
    string name = exeName;
    if (name.length() > 4 && name.substr(name.length() - 4) == ".exe") name = name.substr(0, name.length() - 4);

    int rev = 0;
    if (GetFcRevision(name, rev)) return "EA SPORTS FC " + to_string(rev);
    if (revision > 0) return "EA SPORTS FC " + to_string(revision);
    return name;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid; free(pImageCodecInfo); return j;
        }
    }
    free(pImageCodecInfo); return -1;
}

string GetBase64Icon(const string& exePath) {
    HICON hIcon = NULL;
    ExtractIconExA(exePath.c_str(), 0, &hIcon, NULL, 1);
    if (!hIcon) return "";

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromHICON(hIcon);
    if (!bmp) { DestroyIcon(hIcon); return ""; }

    CLSID pngClsid; GetEncoderClsid(L"image/png", &pngClsid);
    IStream* pStream = NULL; CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    bmp->Save(pStream, &pngClsid, NULL);

    LARGE_INTEGER liZero = {}; ULARGE_INTEGER pos = {};
    pStream->Seek(liZero, STREAM_SEEK_SET, &pos);
    STATSTG stg; pStream->Stat(&stg, STATFLAG_NONAME);
    ULONG size = stg.cbSize.LowPart;

    vector<BYTE> buffer(size); ULONG bytesRead = 0;
    pStream->Read(buffer.data(), size, &bytesRead);

    DWORD base64Size = 0;
    CryptBinaryToStringA(buffer.data(), size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Size);
    string base64String(base64Size, '\0');
    CryptBinaryToStringA(buffer.data(), size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &base64String[0], &base64Size);

    pStream->Release(); delete bmp; DestroyIcon(hIcon);
    if (!base64String.empty() && base64String.back() == '\0') base64String.pop_back();
    return "data:image/png;base64," + base64String;
}

TargetInfo ScoutTarget(const string& manualExeName, const string& manualExePath) {
    TargetInfo target = { {}, "", "", 0 };
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return target;

    PROCESSENTRY32 pe32; pe32.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(hSnap, &pe32)) {
        string manualLower = manualExeName;
        transform(manualLower.begin(), manualLower.end(), manualLower.begin(), ::tolower);

        do {
            string exe = WideToAcp(pe32.szExeFile);
            string lower = exe;
            transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            bool isMatch = false;
            int rev = 0;

            string base = lower;
            if (base.length() > 4 && base.substr(base.length() - 4) == ".exe") base = base.substr(0, base.length() - 4);

            if (!manualExeName.empty()) {
                if (lower == manualLower) {
                    isMatch = true;
                    if (!GetFcRevision(base, rev)) rev = 999;
                }
            }
            else if (GetFcRevision(base, rev)) {
                isMatch = true;
            }

            if (isMatch) {
                if (rev > target.revision) {
                    target.revision = rev;
                    target.execName = exe;
                    target.pids.clear();
                    target.pids.push_back(pe32.th32ProcessID);

                    if (!manualExePath.empty()) target.absolutePath = manualExePath;
                    else {
                        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID);
                        if (hProc) {
                            char pathBuf[MAX_PATH]; DWORD sz = MAX_PATH;
                            if (QueryFullProcessImageNameA(hProc, 0, pathBuf, &sz)) target.absolutePath = pathBuf;
                            CloseHandle(hProc);
                        }
                    }
                }
                else if (rev == target.revision && exe == target.execName) {
                    target.pids.push_back(pe32.th32ProcessID);
                }
            }
        } while (Process32Next(hSnap, &pe32));
    }
    CloseHandle(hSnap);
    return target;
}

vector<string> HarvestSockets(const vector<DWORD>& pids) {
    vector<string> peers; DWORD bufSize = 0;
    GetExtendedTcpTable(NULL, &bufSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    PMIB_TCPTABLE_OWNER_PID pTcp = (PMIB_TCPTABLE_OWNER_PID)malloc(bufSize);
    if (pTcp != nullptr) {
        if (GetExtendedTcpTable(pTcp, &bufSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
            for (DWORD i = 0; i < pTcp->dwNumEntries; i++) {
                DWORD owner = pTcp->table[i].dwOwningPid;
                if (find(pids.begin(), pids.end(), owner) != pids.end()) {
                    struct in_addr ip; ip.S_un.S_addr = (u_long)pTcp->table[i].dwRemoteAddr; string ipStr = inet_ntoa(ip);
                    uint32_t ipDex = ntohl(ip.S_un.S_addr); uint8_t o1 = (ipDex >> 24) & 0xFF, o2 = (ipDex >> 16) & 0xFF;
                    if (o1 == 127 || o1 == 10 || (o1 == 192 && o2 == 168) || (o1 == 172 && o2 >= 16 && o2 <= 31) || ipStr == "0.0.0.0") continue;
                    peers.push_back(ipStr);
                }
            }
        }
        free(pTcp);
    }
    sort(peers.begin(), peers.end()); peers.erase(unique(peers.begin(), peers.end()), peers.end());
    return peers;
}