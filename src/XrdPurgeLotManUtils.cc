#include "XrdPurgeLotManUtils.hh"

#include <XrdSys/XrdSysError.hh>

using json = nlohmann::json;

// Given a bitset based on LogMask, return a human-readable string of the set
// logging levels.
std::string XrootdLotMan::LogMaskToString(int mask) {
	if (mask == 0)
		return "none";
	if (mask >= LogMask::Trace)
		return "trace";
	if (mask >= LogMask::Debug)
		return "debug";
	if (mask >= LogMask::Info)
		return "info";
	if (mask >= LogMask::Warning)
		return "warning";
	if (mask >= LogMask::Error)
		return "error";
	return "unknown";
}

// Function to convert char*** to std::string for logging
std::string XrootdLotMan::convertListToString(char **stringArr) {
	if (stringArr == nullptr) {
		return "";
	}
	std::string result;
	for (int i = 0; stringArr[i] != nullptr; ++i) {
		if (i > 0) {
			result += ", ";
		}
		result += stringArr[i];
	}
	return result;
}

// Given a DirNode, convert it to the JSON object used by LotMan for updating
// lot usage
json XrootdLotMan::dirNodeToJson(const DirNode *node,
								 const XrdPfc::DataFsPurgeshot &purge_shot,
								 XrdSysError &log) {
	json dirJson;
	std::filesystem::path dirPath(node->path);
	dirJson["path"] = dirPath.filename().string();

	const auto usage = purge_shot.find_dir_usage_for_dir_path(node->path);
	if (usage) {
		dirJson["size_GB"] =
			(static_cast<double>(usage->m_StBlocks) * BLKSZ) / GB2B;
	} else {
		dirJson["size_GB"] = 0.0;
	}

	if (!node->subDirs.empty()) {
		dirJson["includes_subdirs"] = true;
		for (const auto *subDir : node->subDirs) {
			dirJson["subdirs"].push_back(
				dirNodeToJson(subDir, purge_shot, log));
		}
	} else {
		dirJson["includes_subdirs"] = false;
	}

	return dirJson;
}

// Loop over the purge_shot's directory vector, and reconstruct the paths for
// LotMan. Doing this allows us to build a usage update JSON, which tells LotMan
// about our current understanding of cache's disk usage.
json XrootdLotMan::reconstructPathsAndBuildJson(
	const XrdPfc::DataFsPurgeshot &purge_shot, XrdSysError &log) {
	std::unordered_map<int, DirNode> indexToDirNode;
	std::vector<DirNode *> rootDirs;

	for (size_t i = 0; i < purge_shot.m_dir_vec.size(); ++i) {
		const auto &dir_entry = purge_shot.m_dir_vec[i];
		DirNode &dirNode = indexToDirNode[i];
		dirNode.path = dir_entry.m_dir_name;
		if (dir_entry.m_parent != -1) {
			dirNode.path = std::filesystem::path("/") /
						   indexToDirNode[dir_entry.m_parent].path /
						   dirNode.path;
			indexToDirNode[dir_entry.m_parent].subDirs.push_back(&dirNode);
			if (dir_entry.m_parent == 0) {
				rootDirs.push_back(&dirNode);
			}
		}
	}

	json allDirsJson = json::array();
	for (const auto *rootDir : rootDirs) {
		allDirsJson.push_back(dirNodeToJson(rootDir, purge_shot, log));
	}

	return allDirsJson;
}
