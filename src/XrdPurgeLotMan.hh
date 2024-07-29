#ifndef __XRDPURGELOTMAN_HH__
#define __XRDPURGELOTMAN_HH__

#include <XrdPfc/XrdPfcPurgePin.hh>
#include <XrdPfc/XrdPfcDirStateSnapshot.hh>
#include <XrdPfc/XrdPfc.hh>
#include <unordered_set>

#include <iostream>

namespace XrdPfc
{

enum class PurgePolicy {
    PastDel,
    PastExp,
    PastOpp,
    PastDed,
    UnknownPolicy
};

struct PurgeDirCandidateStats {
    PurgeDirCandidateStats() :
        dir_b_to_purge{0},
        dir_b_remaining{0}
    {};
    
    PurgeDirCandidateStats(const long long toPurge, const long long remaining) :
        dir_b_to_purge{toPurge},
        dir_b_remaining{remaining}
    {};

    long long dir_b_to_purge;
    long long dir_b_remaining;
};

std::string getPolicyName(PurgePolicy policy) {
    switch (policy) {
        case PurgePolicy::PastDel:
            return "LotsPastDel";
        case PurgePolicy::PastExp:
            return "LotsPastExp";
        case PurgePolicy::PastOpp:
            return "LotsPastOpp";
        case PurgePolicy::PastDed:
            return "LotsPastDed";
        default:
            return "UnknownPolicy";
    }
}

PurgePolicy getPolicyFromName(std::string policy) {
    if (policy == "del") {
        return PurgePolicy::PastDel;
    } else if (policy == "exp") {
        return PurgePolicy::PastExp;
    } else if (policy == "opp") {
        return PurgePolicy::PastOpp;
    } else if (policy == "ded") {
        return PurgePolicy::PastDed;
    } else {
        return PurgePolicy::UnknownPolicy;
    }
}

class XrdPurgeLotMan : public PurgePin
{
    XrdSysError *log;
public:
    XrdPurgeLotMan();
    virtual ~XrdPurgeLotMan() override;

    const Configuration &conf = Cache::Conf();

    virtual long long GetBytesToRecover(const DataFsPurgeshot&) override;
    virtual bool ConfigPurgePin(const char* params) override;

    long long GetConfiguredHWM();
    long long GetConfiguredLWM();

    // Custom deleter for unique pointers in which LM allocates some memory
    // Used to guarantee we call `lotman_free_string_list` on these pointers
    struct LotDeleter;

    class LotManConfiguration {
    public:
        LotManConfiguration() {}
        LotManConfiguration(std::string lot_home, std::vector<PurgePolicy> policy) : m_lot_home(lot_home), m_policy{policy} {}

        std::string GetLotHome() {
            return m_lot_home;
        }
        void SetLotHome(std::string lot_home) {
            m_lot_home = lot_home;
        }
        std::vector<PurgePolicy> GetPolicy() {
            return m_policy;
        }
        void SetPolicy(std::vector<PurgePolicy> policy) {
            m_policy = policy;
        }

    private:
        std::string m_lot_home;
        std::vector<PurgePolicy> m_policy;
    };

    static const std::map<PurgePolicy, void (XrdPurgeLotMan::*)(const DataFsPurgeshot&, long long&)> policyFunctionMap;
    void applyPolicies(const DataFsPurgeshot &purge_shot, long long &bytesRemaining) {
        for (const auto &policy : m_lotman_conf.GetPolicy()) {
            auto it = policyFunctionMap.find(policy);
            if (it != policyFunctionMap.end()) {
                (this->*(it->second))(purge_shot, bytesRemaining);
            }
        }
    }

protected:
    std::string getLotHome() {
        return m_lotman_conf.GetLotHome();
    }

private:
    std::map<std::string, std::unique_ptr<PurgeDirCandidateStats>> m_purge_dirs;
    LotManConfiguration m_lotman_conf;

    bool validateConfiguration(const char *params);

    // indicates that these tend to clean out an entire lot, such as lots past deletion/expiration
    void completePurgePolicyBase(const DataFsPurgeshot &purgeShot, long long &bytesRemaining, PurgePolicy policy);
    // whereas these only purge some of the storage, such as lots past opportunistic/dedicated storage
    void partialPurgePolicyBase(const DataFsPurgeshot &purgeShot, long long &bytesRemaining, PurgePolicy policy);

    std::map<std::string, long long> getLotUsageMap(char ***lots);

    // Policy implementations
    void lotsPastDelPolicy(const DataFsPurgeshot&, long long &bytesToRecover);
    void lotsPastExpPolicy(const DataFsPurgeshot&, long long &bytesRemaining);
    void lotsPastOppPolicy(const DataFsPurgeshot &purgeShot, long long &bytesRemaining);
    void lotsPastDedPolicy(const DataFsPurgeshot &purgeShot, long long &bytesRemaining);

    long long getTotalUsageB();
    std::map<std::string, long long> lotPerDirUsageB(const std::string &lot, const DataFsPurgeshot &purge_shot);


};

// Initialize the policyFunctionMap
const std::map<PurgePolicy, void (XrdPurgeLotMan::*)(const DataFsPurgeshot&, long long&)> XrdPurgeLotMan::policyFunctionMap = {
    {PurgePolicy::PastDel, &XrdPurgeLotMan::lotsPastDelPolicy},
    {PurgePolicy::PastExp, &XrdPurgeLotMan::lotsPastExpPolicy},
    {PurgePolicy::PastOpp, &XrdPurgeLotMan::lotsPastOppPolicy},
    {PurgePolicy::PastDed, &XrdPurgeLotMan::lotsPastDedPolicy}
};
}

#endif // __XRDPURGELOTMAN_HH__