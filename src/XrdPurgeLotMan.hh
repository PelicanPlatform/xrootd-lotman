#ifndef __XRDPURGELOTMAN_HH__
#define __XRDPURGELOTMAN_HH__

#include <XrdPfc/XrdPfc.hh>
#include <XrdPfc/XrdPfcDirStateSnapshot.hh>
#include <XrdPfc/XrdPfcPurgePin.hh>

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <unordered_set>

#define GB2B 1024ll * 1024ll * 1024ll
#define BLKSZ 512ll

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
// Function to convert char*** to std::string for logging
std::string convertListToString(char **stringArr) {
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

struct DirNode {
	std::filesystem::path path;
	std::vector<DirNode *> subDirs; // Pointers to subdirectories
};

// Given a DirNode, convert it to the JSON object used by LotMan for updating
// lot usage
json dirNodeToJson(const DirNode *node,
				   const XrdPfc::DataFsPurgeshot &purge_shot) {
	nlohmann::json dirJson;
	std::filesystem::path dirPath(node->path);
	dirJson["path"] = dirPath.filename().string();

	const auto usage = purge_shot.find_dir_usage_for_dir_path(node->path);
	if (usage) {
		dirJson["size_GB"] =
			static_cast<double>(usage->m_StBlocks) * BLKSZ / GB2B;
	} else {
		dirJson["size_GB"] = 0.0;
	}

	if (!node->subDirs.empty()) {
		dirJson["includes_subdirs"] = true;
		for (const auto *subDir : node->subDirs) {
			dirJson["subdirs"].push_back(dirNodeToJson(subDir, purge_shot));
		}
	} else {
		dirJson["includes_subdirs"] = false;
	}

	return dirJson;
}

// Loop over the purge_shot's directory vector, and reconstruct the paths for
// LotMan. Doing this allows us to build a usage update JSON, which tells LotMan
// about our current understanding of cache's disk usage.
json reconstructPathsAndBuildJson(const XrdPfc::DataFsPurgeshot &purge_shot) {
	std::unordered_map<int, DirNode> indexToDirNode;
	std::vector<DirNode *> rootDirs;

	for (int i = 0; i < purge_shot.m_dir_vec.size(); ++i) {
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

	nlohmann::json allDirsJson = nlohmann::json::array();
	for (const auto *rootDir : rootDirs) {
		allDirsJson.push_back(dirNodeToJson(rootDir, purge_shot));
	}

	return allDirsJson;
}
} // End of anonymous namespace

namespace XrdPfc {

enum class PurgePolicy { PastDel, PastExp, PastOpp, PastDed, UnknownPolicy };

struct PurgeDirCandidateStats {
	PurgeDirCandidateStats() : dir_b_to_purge{0}, dir_b_remaining{0} {};

	PurgeDirCandidateStats(const long long toPurge, const long long remaining)
		: dir_b_to_purge{toPurge}, dir_b_remaining{remaining} {};

	long long dir_b_to_purge;
	long long dir_b_remaining;
};

std::string getPolicyName(PurgePolicy policy);
PurgePolicy getPolicyFromConfigName(const std::string &name);

class XrdPurgeLotMan : public PurgePin {
	XrdSysError *log;

  public:
	XrdPurgeLotMan();
	virtual ~XrdPurgeLotMan() override;

	const Configuration &conf = Cache::Conf();

	virtual long long GetBytesToRecover(const DataFsPurgeshot &) override;
	virtual bool ConfigPurgePin(const char *params) override;

	long long GetConfiguredHWM();
	long long GetConfiguredLWM();

	// Custom deleter for unique pointers in which LM allocates some memory
	// Used to guarantee we call `lotman_free_string_list` on these pointers
	struct LotDeleter;

	class LotManConfiguration {
	  public:
		LotManConfiguration() {}
		LotManConfiguration(std::string lot_home,
							std::vector<PurgePolicy> policy)
			: m_lot_home(lot_home), m_policy{policy} {}

		std::string GetLotHome() { return m_lot_home; }
		void SetLotHome(std::string lot_home) { m_lot_home = lot_home; }
		std::vector<PurgePolicy> GetPolicy() { return m_policy; }
		void SetPolicy(std::vector<PurgePolicy> policy) { m_policy = policy; }

	  private:
		std::string m_lot_home;
		std::vector<PurgePolicy> m_policy;
	};

	static const std::map<PurgePolicy,
						  void (XrdPurgeLotMan::*)(const DataFsPurgeshot &,
												   long long &)> &
	getPolicyFunctionMap() {
		static const std::map<PurgePolicy,
							  void (XrdPurgeLotMan::*)(const DataFsPurgeshot &,
													   long long &)>
			policyFunctionMap = {
				{PurgePolicy::PastDel, &XrdPurgeLotMan::lotsPastDelPolicy},
				{PurgePolicy::PastExp, &XrdPurgeLotMan::lotsPastExpPolicy},
				{PurgePolicy::PastOpp, &XrdPurgeLotMan::lotsPastOppPolicy},
				{PurgePolicy::PastDed, &XrdPurgeLotMan::lotsPastDedPolicy}};
		return policyFunctionMap;
	}

	void applyPolicies(const DataFsPurgeshot &purge_shot,
					   long long &bytesRemaining) {
		for (const auto &policy : m_lotman_conf.GetPolicy()) {
			auto it = getPolicyFunctionMap().find(policy);
			if (it != getPolicyFunctionMap().end()) {
				(this->*(it->second))(purge_shot, bytesRemaining);
			}
		}
	}

  protected:
	std::string getLotHome() { return m_lotman_conf.GetLotHome(); }

  private:
	std::map<std::string, std::unique_ptr<PurgeDirCandidateStats>> m_purge_dirs;
	LotManConfiguration m_lotman_conf;

	bool validateConfiguration(const char *params);

	// indicates that these tend to clean out an entire lot, such as lots past
	// deletion/expiration
	void completePurgePolicyBase(const DataFsPurgeshot &purgeShot,
								 long long &bytesRemaining, PurgePolicy policy);
	// whereas these only purge some of the storage, such as lots past
	// opportunistic/dedicated storage
	void partialPurgePolicyBase(const DataFsPurgeshot &purgeShot,
								long long &bytesRemaining, PurgePolicy policy);

	std::map<std::string, long long> getLotUsageMap(char ***lots);

	// Policy implementations
	void lotsPastDelPolicy(const DataFsPurgeshot &, long long &bytesToRecover);
	void lotsPastExpPolicy(const DataFsPurgeshot &, long long &bytesRemaining);
	void lotsPastOppPolicy(const DataFsPurgeshot &purgeShot,
						   long long &bytesRemaining);
	void lotsPastDedPolicy(const DataFsPurgeshot &purgeShot,
						   long long &bytesRemaining);

	long long getTotalUsageB();
	std::map<std::string, long long>
	lotPerDirUsageB(const std::string &lot, const DataFsPurgeshot &purge_shot);
};

} // namespace XrdPfc

#endif // __XRDPURGELOTMAN_HH__