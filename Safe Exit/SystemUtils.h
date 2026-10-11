#pragma once
#include <windows.h>
#include <string>

bool IsUserAdmin();
void ElevateAndExit();
bool VerifyConnectivity();
bool EnsureFirewallEnabled();
uint32_t IpToUint(const std::string& ipStr);
std::string UintToIp(uint32_t ip);
std::string FormatRange(uint32_t start, uint32_t end);