#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <set>


std::vector<std::string> ComputeInverseRanges(const std::set<std::string>& whitelistedIps);
void RemoveLockdownRules();
void ApplyLockdown(const std::string& gamePath, const std::vector<std::string>& inverseRanges);