#include "Engine.h"
#include "SystemUtils.h"
#include <netfw.h>
#include <comdef.h>
#include <algorithm>
#include <regex>

using namespace std;

const string BARRICADE_IN = "SAFE_EXIT_IN_";
const string BARRICADE_OUT = "SAFE_EXIT_OUT_";

struct IPRange {
    uint32_t start; uint32_t end;
    bool operator<(const IPRange& o) const { return start < o.start; }
};

vector<string> ComputeInverseRanges(const set<string>& whitelistedIps) {
    vector<IPRange> list = {
        {IpToUint("127.0.0.0"), IpToUint("127.255.255.255")}, {IpToUint("10.0.0.0"), IpToUint("10.255.255.255")},
        {IpToUint("172.16.0.0"), IpToUint("172.31.255.255")}, {IpToUint("192.168.0.0"), IpToUint("192.168.255.255")}
    };
    for (const string& ip : whitelistedIps) { uint32_t val = IpToUint(ip); list.push_back({ val, val }); }
    sort(list.begin(), list.end());

    vector<IPRange> merged;
    if (!list.empty()) {
        IPRange item = list[0];
        for (size_t i = 1; i < list.size(); i++) {
            IPRange r = list[i];
            if (r.start <= item.end || r.start == item.end + 1) { if (r.end > item.end) item.end = r.end; }
            else { merged.push_back(item); item = r; }
        }
        merged.push_back(item);
    }

    uint32_t cursor = 0; vector<string> voids;
    for (const auto& item : merged) {
        if (item.start > cursor) voids.push_back(FormatRange(cursor, item.start - 1));
        if (item.end == 0xFFFFFFFF) { cursor = 0xFFFFFFFF; break; }
        cursor = item.end + 1;
    }
    if (cursor < 0xFFFFFFFF) voids.push_back(FormatRange(cursor, 0xFFFFFFFF));
    return voids;
}

void RemoveLockdownRules() {
    INetFwPolicy2* pPolicy = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2), (void**)&pPolicy))) return;
    INetFwRules* pRules = nullptr; pPolicy->get_Rules(&pRules);
    IUnknown* pEnum = nullptr; pRules->get__NewEnum(&pEnum);
    IEnumVARIANT* pVariant = nullptr; pEnum->QueryInterface(__uuidof(IEnumVARIANT), (void**)&pVariant);

    VARIANT var; VariantInit(&var); vector<wstring> rulesToDelete;
    while (pVariant->Next(1, &var, nullptr) == S_OK) {
        INetFwRule* pRule = nullptr; var.pdispVal->QueryInterface(__uuidof(INetFwRule), (void**)&pRule);
        BSTR bName; pRule->get_Name(&bName); string name = _com_util::ConvertBSTRToString(bName);
        if (name.rfind(BARRICADE_IN, 0) == 0 || name.rfind(BARRICADE_OUT, 0) == 0) rulesToDelete.push_back(wstring(bName));
        SysFreeString(bName); pRule->Release(); VariantClear(&var);
    }
    for (const auto& rName : rulesToDelete) { BSTR b = SysAllocString(rName.c_str()); pRules->Remove(b); SysFreeString(b); }
    pVariant->Release(); pEnum->Release(); pRules->Release(); pPolicy->Release();
}

void ApplyLockdown(const string& gamePath, const vector<string>& inverseRanges) {
    if (inverseRanges.empty()) return;
    INetFwPolicy2* pPolicy = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2), (void**)&pPolicy))) return;

    INetFwRules* pRules = nullptr; pPolicy->get_Rules(&pRules);
    const int CHUNK = 200;
    for (size_t i = 0; i < inverseRanges.size(); i += CHUNK) {
        string text = "";
        for (size_t j = i; j < i + CHUNK && j < inverseRanges.size(); j++) text += inverseRanges[j] + ",";
        text.pop_back();

        string suffix = std::to_string(i / CHUNK + 1);

        INetFwRule* pRuleIn = nullptr; CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwRule), (void**)&pRuleIn);
        pRuleIn->put_Name(_bstr_t((BARRICADE_IN + suffix).c_str())); pRuleIn->put_Direction(NET_FW_RULE_DIR_IN);
        pRuleIn->put_Action(NET_FW_ACTION_BLOCK); pRuleIn->put_Protocol(256);
        pRuleIn->put_ApplicationName(_bstr_t(gamePath.c_str())); pRuleIn->put_RemoteAddresses(_bstr_t(text.c_str()));
        pRuleIn->put_Enabled(VARIANT_TRUE); pRules->Add(pRuleIn); pRuleIn->Release();

        INetFwRule* pRuleOut = nullptr; CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwRule), (void**)&pRuleOut);
        pRuleOut->put_Name(_bstr_t((BARRICADE_OUT + suffix).c_str())); pRuleOut->put_Direction(NET_FW_RULE_DIR_OUT);
        pRuleOut->put_Action(NET_FW_ACTION_BLOCK); pRuleOut->put_Protocol(256);
        pRuleOut->put_ApplicationName(_bstr_t(gamePath.c_str())); pRuleOut->put_RemoteAddresses(_bstr_t(text.c_str()));
        pRuleOut->put_Enabled(VARIANT_TRUE); pRules->Add(pRuleOut); pRuleOut->Release();
    }
    pRules->Release(); pPolicy->Release();
}