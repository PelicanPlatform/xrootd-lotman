#ifndef __XRDPURGELOTMAN_HH__
#define __XRDPURGELOTMAN_HH__

#include <XrdPfc/XrdPfc.hh>
#include <XrdPfc/XrdPfcDirStatePurgeshot.hh>
#include <XrdPfc/XrdPfcPurgePin.hh>

#include <filesystem>
#include <map>
#include <unordered_set>

class XrdOucGatherConf;
class XrdSysError;

namespace fs = std::filesystem;

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

  public:
	XrdPurgeLotMan(XrdSysError &log);
	virtual ~XrdPurgeLotMan() override;

	const Configuration &conf = Cache::Conf();
	bool ConfigLog(XrdOucGatherConf &conf, XrdSysError &log);

	virtual long long GetBytesToRecover(const DataFsPurgeshot &) override;
	virtual bool ConfigPurgePin(const char *params) override;

	long long GetConfiguredHWM();
	long long GetConfiguredLWM();
	long long GetConfiguredFUsageBaseline();
	long long GetConfiguredFUsageNominal();
	long long GetConfiguredFUsageMax();

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
	XrdSysError &m_log;

	std::string getLotHome() { return m_lotman_conf.GetLotHome(); }

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

	// Discover an authorised caller string by reading the recursive owners
	// of the `root` lot and installing the first one in lotman's caller
	// context. lotman authorises mutations (reclaim, update, remove) by
	// walking each lot's recursive parent chain and accepting the call if
	// the caller matches any ancestor's owner. Since every lot eventually
	// descends from `root`, matching root's owner authorises us against
	// the whole tree.
	//
	// Returns true when a caller was successfully installed. Returns false
	// (without modifying caller context) when the `root` lot doesn't yet
	// exist in the database — typical on a freshly-bootstrapped cache
	// before any lot has been added — so callers can retry on a later
	// purge tick.
	bool refreshCallerFromRoot();
};

} // namespace XrdPfc

#endif // __XRDPURGELOTMAN_HH__
