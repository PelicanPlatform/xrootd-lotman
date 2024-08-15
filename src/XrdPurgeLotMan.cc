#include "XrdPurgeLotMan.hh"

#include <lotman/lotman.h>

#include <string>
#include <sstream>

namespace XrdPfc {

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

PurgePolicy getPolicyFromConfigName(const std::string &policy) {
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

XrdPurgeLotMan::XrdPurgeLotMan() :
    m_purge_dirs{},
    log(XrdPfc::Cache::GetInstance().GetLog())
    {}

XrdPurgeLotMan::~XrdPurgeLotMan(){}

long long XrdPurgeLotMan::GetConfiguredHWM() {
    return conf.m_diskUsageHWM;
}

long long XrdPurgeLotMan::GetConfiguredLWM() {
    return conf.m_diskUsageLWM;
}

struct XrdPurgeLotMan::LotDeleter {
    void operator()(char** ptr) {
        lotman_free_string_list(ptr);
    }
};

// Gets all the root lots and tallies up their usage. Used to construct the total
// number of bytes to clear on each purge loop by comparing with configured
// HWM/LWM.
long long XrdPurgeLotMan::getTotalUsageB()
{
    // Get all lots, and aggregate those that are rootly
    char **rawLots = nullptr;
    char *err;
    auto rv = lotman_list_all_lots(&rawLots, &err);
    std::unique_ptr<char*[], LotDeleter> lots(rawLots, LotDeleter());
    if (rv != 0)
    {
        log->Emsg("XrdPurgeLotMan", "getTotalUsageB", ("Error getting all lots: " + std::string(err)).c_str());
        // CAREFUL WITH 0 RETURN. We'll never recover any space.
        return 0;
    }

    // For each lot, check if it's a root lot, and if so get its total usage
    long long totalUsage = 0;
    for (int i = 0; lots[i] != nullptr; ++i)
    {
        std::string lotName = lots[i];
        // Check if the lot is a root lot
        if (int rc = lotman_is_root(lotName.c_str(), &err); rc != 1) {
            // Not root, or an error
            if (rc < 0) {
                log->Emsg("XrdPurgeLotMan", "getTotalUsageB", ("Error checking if lot '" + lotName + "' is root: " + std::string(err)).c_str());
            }

            continue;
        } else {
            // Root lot. Get the total usage.
            json usageQueryJSON;
            usageQueryJSON["lot_name"] = lotName;
            usageQueryJSON["total_GB"] = true;

            char *output;
            rv = lotman_get_lot_usage(usageQueryJSON.dump().c_str(), &output, &err);
            if (rv != 0)
            {
                continue;
            }

            json usageJSON = json::parse(output);
            double totalGB = usageJSON["total_GB"]["total"];
            totalUsage += static_cast<long long>(totalGB * GB2B);
        }
    }

    return totalUsage;
}

// Given a lot name, get its associated directories and deduce their usage from the purge_shot's statistics
std::map<std::string, long long> XrdPurgeLotMan::lotPerDirUsageB(const std::string &lot, const DataFsPurgeshot &purge_shot) {
    std::map<std::string, long long> usageMap;
    char *dirs; // will hold a JSON list of lot usage objects
    char *err;
    auto rv = lotman_get_lot_dirs(lot.c_str(), true, &dirs, &err);
    if (rv != 0)
    {
        log->Emsg("XrdPurgeLotMan", "lotPerDirUsageB", ("Error getting dirs in lot " + lot + ": " + std::string(err)).c_str());
        return usageMap;
    }

    json dirsJSON = json::parse(dirs);
    // Iterate through JSON list of objects, get the name of each directory
    for (const auto& dir : dirsJSON)
    {
        std::string path = dir["path"];
        // Get the usage for the directory
        const DirUsage* dirUsage = purge_shot.find_dir_usage_for_dir_path(path);
        if (dirUsage == nullptr)
        {
            log->Emsg("XrdPurgeLotMan", "lotPerDirUsageB", ("Error finding usage for directory " + path).c_str());
            continue;
        }
        long long bytesToRecover = static_cast<long long>(dirUsage->m_StBlocks) * BLKSZ;
        usageMap[path] = bytesToRecover;
    }

    return usageMap;
}

/*
A set of policy wrappers that call the appropriate underlying type of policy function, which
can either entail a total purge policy (for things like deletable/expired lots) or partial
purge (for lots past dedicated/opportunistic quotas).
*/
void XrdPurgeLotMan::lotsPastDelPolicy(const DataFsPurgeshot &purgeShot, long long &bytesRemaining)
{
    PurgePolicy policy = PurgePolicy::PastDel;
    completePurgePolicyBase(purgeShot, bytesRemaining, policy);
}

void XrdPurgeLotMan::lotsPastExpPolicy(const DataFsPurgeshot &purgeShot, long long &bytesRemaining)
{
    PurgePolicy policy = PurgePolicy::PastExp;
    completePurgePolicyBase(purgeShot, bytesRemaining, policy);
}

void XrdPurgeLotMan::lotsPastOppPolicy(const DataFsPurgeshot &purgeShot, long long &bytesRemaining)
{
    PurgePolicy policy = PurgePolicy::PastOpp;
    partialPurgePolicyBase(purgeShot, bytesRemaining, policy);
}

void XrdPurgeLotMan::lotsPastDedPolicy(const DataFsPurgeshot &purgeShot, long long &bytesRemaining)
{
    PurgePolicy policy = PurgePolicy::PastDed;
    partialPurgePolicyBase(purgeShot, bytesRemaining, policy);
}

/*
END POLICY WRAPPERS
*/

// Scaffolding for policies that require purging the entire lot
void XrdPurgeLotMan::completePurgePolicyBase(const DataFsPurgeshot &purgeShot, long long &globalBRemaining, XrdPfc::PurgePolicy policy) 
{
    char **lots;
    char *err;
    int rv;

    switch(policy)
    {
        case XrdPfc::PurgePolicy::PastDel:
            rv = lotman_get_lots_past_del(true, &lots, &err);
            break;
        case XrdPfc::PurgePolicy::PastExp:
            rv = lotman_get_lots_past_exp(true, &lots, &err);
            break;
    }
    std::unique_ptr<char*[], LotDeleter> lots_total_purge(lots, LotDeleter());
    if (rv != 0)
    {
        log->Emsg("XrdPurgeLotMan", "completePurgePolicyBase", ("Error getting lots for policy " + getPolicyName(policy) + ": " + std::string(err)).c_str());
        return;
    }
    log->Emsg("XrdPurgeLotMan", "completePurgePolicyBase", ("Purge policy " + getPolicyName(policy) + " requires clearing lots: " + convertListToString(lots)).c_str());


    // While there's still global space to clear, get directory usage
    // for each of the directories tied to each lot
    for (int i = 0; lots[i] != nullptr; ++i)
    {
        if (globalBRemaining == 0) {
            break;
        }

        const std::string lotName = lots[i];

        // For each directory tied to a lot, get the usage and cumulatively track
        // how much space we need to clear. This also takes into account other 
        // policies that may have already started aggregating space to clear from 
        // that directory as well.
        std::map<std::string, long long> tmpMap = lotPerDirUsageB(lotName, purgeShot);
        for (const auto& [dir, bytesInDir] : tmpMap)
        {
            if (globalBRemaining <= 0) {
                break;
            }

            long long toRecoverFromDir;
            if (m_purge_dirs.find(dir) != m_purge_dirs.end()) {
                // There's nothing left to clean up in this directory
                if (m_purge_dirs[dir]->dir_b_remaining <= 0) {
                    continue;
                }
                // Clean out the rest of the dir, unless we don't have that much left to clear
                toRecoverFromDir = std::min(m_purge_dirs[dir]->dir_b_remaining, globalBRemaining);
            } else {
                // First time any policy has hit this dir, record it
                toRecoverFromDir = std::min(bytesInDir, globalBRemaining);
                m_purge_dirs[dir] = std::make_unique<PurgeDirCandidateStats>(PurgeDirCandidateStats(0ll, bytesInDir));
            }

            // Tally values
            m_purge_dirs[dir]->dir_b_to_purge += toRecoverFromDir;
            globalBRemaining -= toRecoverFromDir;
            m_purge_dirs[dir]->dir_b_remaining -= toRecoverFromDir;
        }
    }

    return;
}

// Scaffolding for policies that require purging partial lots
void XrdPurgeLotMan::partialPurgePolicyBase(const DataFsPurgeshot &purgeShot, long long &globalBRemaining, XrdPfc::PurgePolicy policy) 
{
    char **lots;
    char *err;
    // TODO: Come back and think about whether we want recursive children here
    //       For now, I'm saying _yes_ because if a child takes up lots of space
    //       but isn't past its own quota, we still want the option to clear it.
    int rv;
    switch(policy)
    {
        case XrdPfc::PurgePolicy::PastOpp:
            rv = lotman_get_lots_past_opp(true, true, &lots, &err);
            break;
        case XrdPfc::PurgePolicy::PastDed:
            rv = lotman_get_lots_past_ded(true, true, &lots, &err);
            break;
    }
    std::unique_ptr<char*[], LotDeleter> lots_partial_purge(lots, LotDeleter());

    if (rv != 0)
    {
        log->Emsg("XrdPurgeLotMan", "partialPurgePolicyBase", ("Error getting lots for policy " + getPolicyName(policy) + ": " + std::string(err)).c_str());
        return;
    }
    log->Emsg("XrdPurgeLotMan", "partialPurgePolicyBase", ("Purge policy " + getPolicyName(policy) + " requires clearing lots: " + convertListToString(lots)).c_str());

    // Get directory usage for each of the directories tied to each lot
    for (int i = 0; lots[i] != nullptr; ++i)
    {
        if (globalBRemaining <= 0) {
            break;
        }

        const std::string lotName = lots[i];

        // if past opp, then toRecover = total_usage - opp_usage - ded_usage
        // if past ded, then toRecover = total_usage - ded_usage
        long long toRecoverFromLot;
        json usageQueryJSON;
        usageQueryJSON["lot_name"] = lotName;
        usageQueryJSON["total_GB"] = true;
        usageQueryJSON["dedicated_GB"] = true;
        if (policy == XrdPfc::PurgePolicy::PastOpp) {
            usageQueryJSON["opportunistic_GB"] = true;
        }

        char *output;
        rv = lotman_get_lot_usage(usageQueryJSON.dump().c_str(), &output, &err);
        if (rv != 0)
        {
            log->Emsg("XrdPurgeLotMan", "partialPurgePolicyBase", ("Error getting lot usage for " + lotName + ": " + std::string(err)).c_str());
            continue;
        }

        json usageJSON = json::parse(output);
        double totalGB = usageJSON["total_GB"]["total"];
        double dedGB = usageJSON["dedicated_GB"]["total"];
        if (policy == XrdPfc::PurgePolicy::PastOpp) {
            double oppGB = usageJSON["opportunistic_GB"]["total"];
            toRecoverFromLot = static_cast<long long>((totalGB - dedGB - oppGB) * GB2B);
        } else {
            toRecoverFromLot = static_cast<long long>((totalGB - dedGB) * GB2B);
        }

        if (toRecoverFromLot > globalBRemaining) {
            toRecoverFromLot = globalBRemaining;
        }

        std::map<std::string, long long> tmpUsage = lotPerDirUsageB(lotName, purgeShot);
        for (const auto& [dir, bytesInDir] : tmpUsage)
        {
            if (globalBRemaining <= 0 || toRecoverFromLot <= 0) {
                break;
            }

            long long toRecoverFromDir;
            if (m_purge_dirs.find(dir) != m_purge_dirs.end()) {
                // There's nothing left to clean up in this directory
                if (m_purge_dirs[dir]->dir_b_remaining <= 0) {
                    continue;
                }

                // there's space left to clear. Get rid of as much of it as we need to
                toRecoverFromDir = std::min(m_purge_dirs[dir]->dir_b_remaining, toRecoverFromLot);
            } else {
                // First time we've seen this dir as a candidate, record it
                toRecoverFromDir = std::min(bytesInDir, toRecoverFromLot);
                m_purge_dirs[dir] = std::make_unique<PurgeDirCandidateStats>(PurgeDirCandidateStats(0ll, bytesInDir));
            }

            m_purge_dirs[dir]->dir_b_to_purge += toRecoverFromDir;
            globalBRemaining -= toRecoverFromDir;
            toRecoverFromLot -= toRecoverFromDir;
            m_purge_dirs[dir]->dir_b_remaining -= toRecoverFromDir;
        }
    }

    return;
}





/*
Handles determining the total number of bytes to recover,
as well as populating the m_list of directories:bytesToRecover the purge cycle
iterates over when clearing stuff.

In LotMan's ideal case, we could say "here's the ordering of directories, keep clearing
until you hit LWM," but the purge code doesn't quite have the logic for that, so handle this
determination in the plugin.
*/
long long XrdPurgeLotMan::GetBytesToRecover(const DataFsPurgeshot &purge_shot)
{
    // reset m_list
    m_list.clear();
    m_purge_dirs.clear();
    
    char *err;
    char *output;
    auto rv = lotman_get_context_str("lot_home", &output, &err);
    if (rv != 0)
    {
        log->Emsg("XrdPurgeLotMan", "GetBytesToRecover", "Error getting lot home:", err);
        // TODO: If we encounter an error and return 0, we'll never recover any space. Need to think about
        // how to fall back to age-based purging when this is the case.
        return 0;
    }
    auto lotUpdateJson = reconstructPathsAndBuildJson(purge_shot);

    rv = lotman_update_lot_usage_by_dir(lotUpdateJson.dump().c_str(), false, &err);
    if (rv != 0)
    {
        log->Emsg("XrdPurgeLotMan", "GetBytesToRecover", "Error updating lot usage by dir:", err);
        // TODO: If we encounter an error and return 0, we'll never recover any space. Need to think about
        // how to fall back to age-based purging when this is the case.
        return 0;
    }


    // Get the total usage across root lots.
    long long totalUsageB = getTotalUsageB();
    if (totalUsageB < GetConfiguredHWM())
    {
        // In this case, it's actually true that we have nothing to recover.
        return 0;
    }

    // We've determined there's something to purge
    long long bytesToRecover = totalUsageB - conf.m_diskUsageLWM;
    long long bytesRemaining = bytesToRecover;
    log->Emsg("XrdPurgeLotMan", "GetBytesToRecover", ("Recoverable bytes: " + std::to_string(bytesToRecover) + " bytes").c_str());

    // Apply the policies to determine how much space to recover from each directory. These are applied in the
    // order configured through the cache's configuration file.
    applyPolicies(purge_shot, bytesRemaining);

    for (const auto & [dir, stats] : m_purge_dirs) {
        DirInfo update;
        update.path = dir;
        update.nBytesToRecover = stats->dir_b_to_purge;

        m_list.push_back(update);
    }

    return bytesToRecover;
}


// Read the cache's purge lib configuration, and apply the policies in the order they're listed.
bool XrdPurgeLotMan::validateConfiguration(const char *params)
{
    LotManConfiguration cfg{};

    // Split params by space
    std::istringstream iss(params);
    std::vector<std::string> paramVec;
    std::string token;
    char delim = ' ';
    
    while (getline(iss, token, delim)) { 
        paramVec.push_back(token); 
    } 

    // At minimum, we have a lot home, and potentially up to 4 policies
    assert(paramVec.size() >= 1);
    assert(paramVec.size() <=5);

    // Get LotHome
    std::filesystem::path lotHome(paramVec[0]);
    if (!std::filesystem::exists(lotHome) && !std::filesystem::is_directory(lotHome))
    {
        log->Emsg("XrdPurgeLotMan", "validateConfiguration", ("The provided lot home of '" + lotHome.string() +"' does not exist.").c_str());
        return false;
    }
    cfg.SetLotHome(lotHome.string());

    std::vector<PurgePolicy> policies;
    std::set<PurgePolicy> encountered;
    for (int i = 1; i < paramVec.size(); ++i)
    {
        PurgePolicy policy = getPolicyFromConfigName(paramVec[i]);
        if (policy == PurgePolicy::UnknownPolicy)
        {
            log->Emsg("XrdPurgeLotMan", "validateConfiguration", ("Unknown policy: " + paramVec[i]).c_str());
            return false;
        }
        // Insert and check for duplicates in one step
        auto result = encountered.insert(policy);
        if (!result.second)
        {
            log->Emsg("XrdPurgeLotMan", "validateConfiguration", ("Duplicate policy detected: " + paramVec[i]).c_str());
            return false;
        }

        policies.push_back(policy);
    }

    // Use default policies if none are provided
    if (policies.empty())
    {
        policies = {PurgePolicy::PastDel, PurgePolicy::PastExp, PurgePolicy::PastOpp, PurgePolicy::PastDed};
    }

    cfg.SetPolicy(policies);
    m_lotman_conf = cfg;

    return true;
}

// Handle configuration for the plugin
bool XrdPurgeLotMan::ConfigPurgePin(const char* params)
{
    (void)params; // Avoid unused parameter warning

    if (!validateConfiguration(params)) {
        log->Emsg("XrdPurgeLotMan", "ConfigPurgePin", "Configuration validation failed.");
        return false;
    };
    
    char *err;
    auto rv = lotman_set_context_str("lot_home", getLotHome().c_str(), &err);
    if (rv != 0)
    {
        log->Emsg("XrdPurgeLotMan", "ConfigPurgePin", ("Error setting lot home to '" +  getLotHome() + "': " + std::string(err)).c_str());
        return false;
    }

    return true;
}

} // End of namespace XrdPfc


/******************************************************************************/
/*                          XrdPfcGetPurgePin                                 */
/******************************************************************************/

// Return a purge object to use.
extern "C"
{
   XrdPfc::PurgePin *XrdPfcGetPurgePin(XrdSysError &)
   {
      return new XrdPfc::XrdPurgeLotMan();
   }
}
