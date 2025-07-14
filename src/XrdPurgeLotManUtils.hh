#pragma once
#include "XrdPfc/XrdPfcDirStatePurgeshot.hh"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class XrdSysError;
namespace XrootdLotMan {

constexpr long long GB2B = 1000ll * 1000ll * 1000ll;
constexpr long long BLKSZ = 512ll;

enum LogMask {
	None = 0x00,
	Error = 0x01,
	Warning = 0x02,
	Info = 0x04,
	Debug = 0x08,
	Trace = 0x10
};

struct DirNode {
	std::filesystem::path path;
	std::vector<DirNode *> subDirs;
};

std::string LogMaskToString(int mask);

std::string convertListToString(char **stringArr);

nlohmann::json dirNodeToJson(const DirNode *node,
							 const XrdPfc::DataFsPurgeshot &purge_shot,
							 XrdSysError &log);

nlohmann::json
reconstructPathsAndBuildJson(const XrdPfc::DataFsPurgeshot &purge_shot,
							 XrdSysError &log);

} // namespace XrootdLotMan
