#include "../src/XrdPurgeLotMan.hh"
#include "../src/XrdPurgeLotManUtils.hh"

#include <XrdPfc/XrdPfc.hh>
#include <XrdSys/XrdSysError.hh>
#include <XrdSys/XrdSysLogger.hh>
#include <lotman/lotman.h>

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

// Should be overriden by CMake
#ifndef TEST_RESOURCES_DIR
#define TEST_RESOURCES_DIR "<build dir>/test/resources"
#endif

using json = nlohmann::json;
using namespace XrootdLotMan;

class LMSetupTeardown : public ::testing::Test {
  protected:
	static std::string tmp_dir;

	static std::string create_temp_directory() {
		// Generate a unique name for the temporary directory
		std::string temp_dir_template = "/tmp/purge_pin_test_XXXXXX";
		char temp_dir_name[temp_dir_template.size() +
						   1]; // +1 for the null-terminator
		std::strcpy(temp_dir_name, temp_dir_template.c_str());

		// mkdtemp replaces 'X's with a unique directory name and creates the
		// directory
		char *mkdtemp_result = mkdtemp(temp_dir_name);
		if (mkdtemp_result == nullptr) {
			std::cerr << "Error creating temp directory: " << strerror(errno)
					  << std::endl;
			exit(1);
		}

		return std::string(mkdtemp_result);
	}

	static void SetUpTestSuite() {
		tmp_dir = create_temp_directory();
		char *err;
		auto rv = lotman_set_context_str("lot_home", tmp_dir.c_str(), &err);
		if (rv != 0) {
			std::cerr << "Error setting lot_home: " << err << std::endl;
			exit(1);
		}

		rv = lotman_set_context_str("caller", "owner1", &err);
		if (rv != 0) {
			std::cerr << "Error setting caller: " << err << std::endl;
			exit(1);
		}
	}

	static void TearDownTestSuite() { std::filesystem::remove_all(tmp_dir); }
};

std::string LMSetupTeardown::tmp_dir;

class XrdPurgeLotManTest : public XrdPfc::XrdPurgeLotMan {
  public:
	XrdSysLogger log{};
	XrdSysError err{&log, "TestXrdPurgeLotMan"};

	XrdPurgeLotManTest() : XrdPurgeLotMan(err) {}

	XrdPurgeLotManTest(const std::string &configfn) : XrdPurgeLotMan(err) {
		// TEST_RESOURCES_DIR is a macro defined in the test directory's
		// CMakeLists.txt.
		// It will point to <build dir>/test/resources
		std::string configPath =
			std::string(TEST_RESOURCES_DIR) + "/" + configfn;
		setenv("XRDCONFIGFN", configPath.c_str(), 1); // 1 = overwrite
		// Also need to set some xrootd instance for XrdOucGather to function
		// properly
		setenv("XRDINSTANCE", "test foo@bar", 1);
	}

	~XrdPurgeLotManTest() override = default;

	long long testGetTotalUsageB() { return getTotalUsageB(); }
	LotManConfiguration testGetLotmanConf() { return m_lotman_conf; }
	// Forwarder so tests can exercise the protected lotPerDirUsageB
	// directly. Returning a map (rather than just a status) lets the test
	// assert both that the call completes and that missing-DirState paths
	// are silently omitted, while paths that do have entries land in the
	// map with the expected byte counts.
	std::map<std::string, long long>
	testLotPerDirUsageB(const std::string &lot,
						const XrdPfc::DataFsPurgeshot &purge_shot) {
		return lotPerDirUsageB(lot, purge_shot);
	}
	std::string GetLogLevelString() const {
		return LogMaskToString(m_log.getMsgMask());
	}
};

void populatePurgeElement(XrdPfc::DirPurgeElement &element,
						  const char *dir_name, int parent, int daughters_begin,
						  int daughters_end) {
	element.m_dir_name = dir_name;
	element.m_parent = parent;
	element.m_daughters_begin = daughters_begin;
	element.m_daughters_end = daughters_end;
}

json createLotJSON(const std::string &lot_name, const std::string &owner,
				   const std::string &path, bool recursive, double dedicated_GB,
				   double opportunistic_GB, long long currentTimeMSEpoch,
				   long long expiration_time, long long deletion_time) {
	return {{"lot_name", lot_name},
			{"owner", owner},
			{"parents", {lot_name}},
			{"paths", {{{"path", path}, {"recursive", recursive}}}},
			{"management_policy_attrs",
			 {{"dedicated_GB", dedicated_GB},
			  {"opportunistic_GB", opportunistic_GB},
			  {"max_num_objects", 100},
			  {"creation_time", currentTimeMSEpoch},
			  {"expiration_time", expiration_time},
			  {"deletion_time", deletion_time}}}};
}

// Test cases for convertListToString function
TEST(ConvertListToStringTest, HandlesEmptyArray) {
	char *arr[] = {nullptr};
	std::string result = convertListToString(arr);
	EXPECT_EQ(result, "");
}

TEST(ConvertListToStringTest, HandlesSingleElement) {
	const char *arr[] = {"one", nullptr};
	std::string result = convertListToString(const_cast<char **>(arr));
	EXPECT_EQ(result, "one");
}

TEST(ConvertListToStringTest, HandlesMultipleElements) {
	const char *arr[] = {"one", "two", "three", nullptr};
	std::string result = convertListToString(const_cast<char **>(arr));
	EXPECT_EQ(result, "one, two, three");
}

TEST(ConvertListToStringTest, HandlesNullPointer) {
	char **arr = nullptr;
	std::string result = convertListToString(arr);
	EXPECT_EQ(result, "");
}

TEST(DirNodeToJsonTest, ConstructsJsonForEmptyDirs) {
	// To test the full functionality of this in a way that also reports
	// directory sizes, I'd need to mock XrdPfc::DataFsPurgeshot, which has a
	// non-overridable function for getting the size of a directory. This also
	// requires real directories. Since I'm not testing the underlying
	// XrdPfc::DataFsPurgeshot functionality, I'll just test the JSON conversion
	// here.

	// DirNode is one of the plugin's structs to act as an intermediary between
	// the DataFsFPurgeshot class, the DirPurgeElement class, and the JSON
	// object that LotMan uses to update lot usage.
	DirNode subNode1;
	subNode1.path = "/path/to/dir/subdir1";
	DirNode subNode2;
	subNode2.path = "/path/to/dir/subdir2";
	DirNode subNode3;
	subNode3.path = "/path/to/dir/subdir2/subdir3";

	// Create main node and populate it with sub-nodes
	DirNode node;
	node.path = "/path/to/dir";
	node.subDirs.push_back(&subNode1);
	node.subDirs.push_back(&subNode2);
	subNode2.subDirs.push_back(&subNode3);

	// The purge shot holds DirPurgeElements, where each element specifies the
	// name of the dir (_not_ the complete path), and which indices in the purge
	// shot's vector of DirPurgeElements constitute child directories.
	XrdPfc::DataFsPurgeshot purge_shot;
	XrdPfc::DirPurgeElement rootElement, subElement1, subElement2, subElement3;
	populatePurgeElement(rootElement, "dir", -1, 1, 3);
	populatePurgeElement(subElement1, "subdir1", 0, 0, 0);
	populatePurgeElement(subElement2, "subdir2", 0, 3, 4);
	populatePurgeElement(subElement3, "subdir3", 3, 0, 0);

	purge_shot.m_dir_vec.push_back(rootElement);
	purge_shot.m_dir_vec.push_back(subElement1);
	purge_shot.m_dir_vec.push_back(subElement2);
	purge_shot.m_dir_vec.push_back(subElement3);

	XrdSysLogger log{};
	XrdSysError err{&log, "TestXrdPurgeLotMan"};
	json result = dirNodeToJson(&node, purge_shot, err);

	// Validatation
	EXPECT_EQ(result["path"], "dir");
	EXPECT_EQ(result["size_GB"], 0.0);
	EXPECT_EQ(result["includes_subdirs"], true);
	EXPECT_EQ(result["subdirs"].size(), 2);
	EXPECT_EQ(result["subdirs"][0]["path"], "subdir1");
	EXPECT_EQ(result["subdirs"][0]["size_GB"], 0.0);
	EXPECT_EQ(result["subdirs"][0]["includes_subdirs"], false);
	EXPECT_EQ(result["subdirs"][1]["path"], "subdir2");
	EXPECT_EQ(result["subdirs"][1]["size_GB"], 0.0);
	EXPECT_EQ(result["subdirs"][1]["includes_subdirs"], true);
	EXPECT_EQ(result["subdirs"][1]["subdirs"].size(), 1);
	EXPECT_EQ(result["subdirs"][1]["subdirs"][0]["path"], "subdir3");
	EXPECT_EQ(result["subdirs"][1]["subdirs"][0]["size_GB"], 0.0);
}

TEST(reconstructPathsAndBuildJson, TypicalCase) {
	// Given a constructed DataFsPurgeshot, reconstruct the paths and build a
	// JSON object.

	// Again, the test won't actually check sizes, because that requires a
	// mocked DataFSPurgeshot, or the construction of a real one that points to
	// real directories.
	XrdPfc::DataFsPurgeshot purge_shot;
	// For some reason, it seems like the root element always has an empty
	// directory name. Letting this creep into the LotUpdate JSON object would
	// throw Lotman off, so the reconstruction function needs to handle this.
	XrdPfc::DirPurgeElement rootElement, parentElement, subElement1,
		subElement2, subElement3;
	populatePurgeElement(rootElement, "", -1, 0, 4);
	populatePurgeElement(parentElement, "dir", 0, 1, 3);
	populatePurgeElement(subElement1, "subdir1", 1, 0, 0);
	populatePurgeElement(subElement2, "subdir2", 1, 3, 4);
	populatePurgeElement(subElement3, "subdir3", 3, 0, 0);

	purge_shot.m_dir_vec.push_back(rootElement);
	purge_shot.m_dir_vec.push_back(parentElement);
	purge_shot.m_dir_vec.push_back(subElement1);
	purge_shot.m_dir_vec.push_back(subElement2);
	purge_shot.m_dir_vec.push_back(subElement3);

	XrdSysLogger log{};
	XrdSysError err{&log, "TestXrdPurgeLotMan"};
	json result = reconstructPathsAndBuildJson(purge_shot, err);

	// Validation
	EXPECT_EQ(result.size(), 1);
	EXPECT_EQ(result[0]["path"], "dir");
	EXPECT_EQ(result[0]["size_GB"], 0.0);
	EXPECT_EQ(result[0]["includes_subdirs"], true);
	EXPECT_EQ(result[0]["subdirs"].size(), 2);
	EXPECT_EQ(result[0]["subdirs"][0]["path"], "subdir1");
	EXPECT_EQ(result[0]["subdirs"][0]["size_GB"], 0.0);
	EXPECT_EQ(result[0]["subdirs"][0]["includes_subdirs"], false);
	EXPECT_EQ(result[0]["subdirs"][1]["path"], "subdir2");
	EXPECT_EQ(result[0]["subdirs"][1]["size_GB"], 0.0);
	EXPECT_EQ(result[0]["subdirs"][1]["includes_subdirs"], true);
	EXPECT_EQ(result[0]["subdirs"][1]["subdirs"].size(), 1);
	EXPECT_EQ(result[0]["subdirs"][1]["subdirs"][0]["path"], "subdir3");
	EXPECT_EQ(result[0]["subdirs"][1]["subdirs"][0]["size_GB"], 0.0);
}

TEST(GetPolicyNameTest, ReturnsCorrectPolicyName) {
	EXPECT_EQ(XrdPfc::getPolicyName(XrdPfc::PurgePolicy::PastDel),
			  "LotsPastDel");
	EXPECT_EQ(XrdPfc::getPolicyName(XrdPfc::PurgePolicy::PastExp),
			  "LotsPastExp");
	EXPECT_EQ(XrdPfc::getPolicyName(XrdPfc::PurgePolicy::PastOpp),
			  "LotsPastOpp");
	EXPECT_EQ(XrdPfc::getPolicyName(XrdPfc::PurgePolicy::PastDed),
			  "LotsPastDed");
	EXPECT_EQ(XrdPfc::getPolicyName(XrdPfc::PurgePolicy::UnknownPolicy),
			  "UnknownPolicy");
}

TEST(GetPolicyFromConfigNameTest, ReturnsCorrectPolicyEnum) {
	// These "names" come from the cache configuration. They're abbreviated to
	// match the general "xrootd" style of doing things, so they're not the same
	// as the names returned by getPolicyName.
	EXPECT_EQ(XrdPfc::getPolicyFromConfigName("del"),
			  XrdPfc::PurgePolicy::PastDel);
	EXPECT_EQ(XrdPfc::getPolicyFromConfigName("exp"),
			  XrdPfc::PurgePolicy::PastExp);
	EXPECT_EQ(XrdPfc::getPolicyFromConfigName("opp"),
			  XrdPfc::PurgePolicy::PastOpp);
	EXPECT_EQ(XrdPfc::getPolicyFromConfigName("ded"),
			  XrdPfc::PurgePolicy::PastDed);
	EXPECT_EQ(XrdPfc::getPolicyFromConfigName("foobar"),
			  XrdPfc::PurgePolicy::UnknownPolicy);
	EXPECT_EQ(XrdPfc::getPolicyFromConfigName(""),
			  XrdPfc::PurgePolicy::UnknownPolicy);
}

TEST_F(LMSetupTeardown, GetTotalUsageBTest) {
	// Create a few lots
	// Current time in milliseconds since epoch

#ifndef SECONDS
#define SECONDS 1000
#endif

	auto currentTimeMSEpoch =
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count();

	json defaultJSON =
		createLotJSON("default", "owner2", "/default", true, 0.032, 0.01,
					  currentTimeMSEpoch, currentTimeMSEpoch + (240 * SECONDS),
					  currentTimeMSEpoch + (300 * SECONDS));
	json lot1JSON =
		createLotJSON("lot1", "owner1", "/lot1", false, 0.017, 0.01,
					  currentTimeMSEpoch, currentTimeMSEpoch + (30 * SECONDS),
					  currentTimeMSEpoch + (60 * SECONDS));
	json lot2JSON =
		createLotJSON("lot2", "owner1", "/lot2", false, 0.011, 0.011,
					  currentTimeMSEpoch, currentTimeMSEpoch + (480 * SECONDS),
					  currentTimeMSEpoch + (480 * SECONDS));
	// lot3 is child of lot2 so we can use it to test total usage calculations.
	// Because the path for lot3 isn't under one of lot2's paths, its usage will
	// be counted along with lot2's personal usage See the lotman repo for more
	// information about how usage is calculated.
	json lot3JSON =
		createLotJSON("lot3", "owner1", "/lot3", true, 0.011, 0.011,
					  currentTimeMSEpoch, currentTimeMSEpoch + (480 * SECONDS),
					  currentTimeMSEpoch + (480 * SECONDS));
	// lot4 is also a sublot of lot2, but it's scoped under one of lot2's paths
	// unlike lot3. Because we're building directory updates to update LotMan's
	// usage metrics, lot4's usage should NOT be added to the number we pass for
	// usage under the path /lot2.
	json lot4JSON =
		createLotJSON("lot4", "owner1", "/lot2/lot4", true, 0.01, 0.011,
					  currentTimeMSEpoch, currentTimeMSEpoch + (480 * SECONDS),
					  currentTimeMSEpoch + (480 * SECONDS));

	// Store the JSON strings in intermediary vars (can't just do
	// .dump().c_str() because dump returns a pointer to a temporary object)
	std::string defaultLotStr = defaultJSON.dump();
	std::string lot1Str = lot1JSON.dump();
	std::string lot2Str = lot2JSON.dump();
	std::string lot3Str = lot3JSON.dump();
	std::string lot4Str = lot4JSON.dump();

	const char *defaultLot = defaultLotStr.c_str();
	const char *lot1 = lot1Str.c_str();
	const char *lot2 = lot2Str.c_str();
	const char *lot3 = lot3Str.c_str();
	const char *lot4 = lot4Str.c_str();
	std::vector<const char *> lots = {defaultLot, lot1, lot2, lot3, lot4};

	char *err;
	int rv;
	for (const char *lot : lots) {
		rv = lotman_add_lot(lot, &err);
		ASSERT_TRUE(rv == 0) << err;
	}

	// Populate their usage
	json usageUpdateJSON = json::array(
		{{{"path", "foo"},
		  {"size_GB", 32.1},
		  {"includes_subdirs", true},
		  {"subdirs",
		   {{{"path", "bar"},
			 {"size_GB", 12.1},
			 {"includes_subdirs", false}}}}},
		 {{"path", "lot1"}, {"size_GB", 12.3}, {"includes_subdirs", false}},
		 {{"path", "lot2"},
		  {"size_GB", 3434.0},
		  {"includes_subdirs", true},
		  {"subdirs",
		   {{{"path", "lot4"},
			 {"size_GB", 3100.0},
			 {"includes_subdirs", false}}}}},
		 {{"path", "lot3"},
		  {"size_GB", 3333.1},
		  {"includes_subdirs", true},
		  {"subdirs",
		   {{{"path", "sub-lot3"},
			 {"size_GB", 12.1},
			 {"includes_subdirs", false}}}}}});

	std::string usageUpdateStr = usageUpdateJSON.dump();
	const char *usageUpdateCStr = usageUpdateStr.c_str();
	rv = lotman_update_lot_usage_by_dir(usageUpdateCStr, false,
										static_cast<int64_t>(currentTimeMSEpoch),
										&err);
	ASSERT_TRUE(rv == 0) << err;

	long long totalUsage;
	// shouldn't double count usage of lot3.
	long long expectedUsage = (32.1 + 12.3 + 3434.0 + 3333.1) * GB2B;
	XrdPurgeLotManTest testPurgePin{};
	totalUsage = testPurgePin.testGetTotalUsageB();

	ASSERT_TRUE(totalUsage == expectedUsage)
		<< "Expected usage: " << expectedUsage
		<< " Actual usage: " << totalUsage;
}

TEST_F(LMSetupTeardown, ValidPurgePinConfigTest) {
	using namespace XrdPfc;

	std::string lotHome = LMSetupTeardown::tmp_dir;
	std::string configParams = lotHome + " exp opp ded";

	XrdPurgeLotManTest testPurgePin{"trace-log-level.cfg"};
	bool rv = testPurgePin.ConfigPurgePin(configParams.c_str());
	ASSERT_TRUE(rv);
	EXPECT_EQ(testPurgePin.GetLogLevelString(), "trace");

	std::vector<PurgePolicy> expectedPolicies = {
		PurgePolicy::PastExp, PurgePolicy::PastOpp, PurgePolicy::PastDed};
	XrdPurgeLotMan::LotManConfiguration lotmanConf =
		testPurgePin.testGetLotmanConf();
	EXPECT_EQ(lotHome, lotmanConf.GetLotHome());
	EXPECT_EQ(expectedPolicies, lotmanConf.GetPolicy());

	// Passing w/ no policies should result in default
	configParams = lotHome;
	rv = testPurgePin.ConfigPurgePin(configParams.c_str());
	ASSERT_TRUE(rv);
	expectedPolicies = {PurgePolicy::PastDel, PurgePolicy::PastExp,
						PurgePolicy::PastOpp, PurgePolicy::PastDed};
	lotmanConf = testPurgePin.testGetLotmanConf();
	EXPECT_EQ(lotHome, lotmanConf.GetLotHome());
	EXPECT_EQ(expectedPolicies, lotmanConf.GetPolicy());
}

/*
Punting on this test for now, because I can't figure out how to set up the
xrootd logger in a way that doesn't segfault when I hit log->Emsg in the errors
this generates. TEST_F(LMSetupTeardown, InvalidPurgePinConfigTest) { using
namespace XrdPfc;

	// Pass a config w/ no lot home
	std::string configParams = "ded opp del exp";

	XrdPurgeLotManTest testPurgePin{};
	bool rv = testPurgePin.ConfigPurgePin(configParams.c_str());
	ASSERT_FALSE(rv);

	// Typo in policy name
	configParams = LMSetupTeardown::tmp_dir + " dedd opp del exp";
	rv = testPurgePin.ConfigPurgePin(configParams.c_str());
	ASSERT_FALSE(rv);
}
*/

// Verifies that ConfigPurgePin installs an authorised caller context by
// reading the recursive owners of the `root` lot. The fix replaces an
// earlier behaviour that hard-coded caller="root" — a literal string that
// matches no real owner once lots are created by services like Pelican's
// origins/director that record owners as URLs.
TEST_F(LMSetupTeardown, ConfigPurgePinInstallsRootOwnerAsCaller) {
	using namespace XrdPfc;

	// Stash the existing caller context so this test doesn't bleed state into
	// later tests that rely on caller="owner1" set in SetUpTestSuite.
	char *prevCaller = nullptr;
	char *err = nullptr;
	(void)lotman_get_context_str("caller", &prevCaller, &err);
	std::unique_ptr<char, decltype(&free)> prevCallerOwner(prevCaller, free);

	// Bootstrap the `root` lot owned by a URL-shaped identity, mirroring how
	// Pelican's director registers itself.
	const std::string directorUrl = "https://director.example.org:8443";
	auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					 std::chrono::system_clock::now().time_since_epoch())
					 .count();
	{
		// Need to be acting as the URL owner to add a lot whose owner is
		// the URL — addLot validates caller==owner during creation.
		(void)lotman_set_context_str("caller", directorUrl.c_str(), &err);
		json rootJSON = createLotJSON(
			"root", directorUrl, "/", false, 1.0, 1.0, nowMs - 1000,
			nowMs + (365LL * 24 * 3600 * 1000),
			nowMs + (2LL * 365 * 24 * 3600 * 1000));
		// Ignore failure if `root` already exists from a previous test run
		// inside this suite — the assertion below is what matters.
		(void)lotman_add_lot(rootJSON.dump().c_str(), &err);
	}

	std::string lotHome = LMSetupTeardown::tmp_dir;
	std::string configParams = lotHome;
	XrdPurgeLotManTest testPurgePin{"trace-log-level.cfg"};
	ASSERT_TRUE(testPurgePin.ConfigPurgePin(configParams.c_str()));

	char *caller = nullptr;
	auto rv = lotman_get_context_str("caller", &caller, &err);
	std::unique_ptr<char, decltype(&free)> callerOwner(caller, free);
	ASSERT_EQ(rv, 0) << (err ? err : "(null)");
	ASSERT_NE(caller, nullptr);
	EXPECT_STREQ(caller, directorUrl.c_str())
		<< "ConfigPurgePin must install the recursive owner of the `root` "
		   "lot as the caller, not the literal string \"root\"";

	// Restore previous caller for downstream tests. lotman context is
	// process-global so leaking the director URL would break later tests
	// that expect to act as a different identity.
	const char *toRestore = prevCaller ? prevCaller : "owner1";
	(void)lotman_set_context_str("caller", toRestore, &err);
}

// End-to-end regression for the production crash where the cache plugin
// could not reclaim lots created by origins because caller="root" (a literal
// string) did not match any owner. After the fix the caller is `root` lot's
// owner, which authorises mutations on every descendant via the recursive
// parent-chain check inside lotman.
TEST_F(LMSetupTeardown, ConfigPurgePinAuthorizesReclaimOfDifferentlyOwnedLot) {
	using namespace XrdPfc;

	char *prevCaller = nullptr;
	char *err = nullptr;
	(void)lotman_get_context_str("caller", &prevCaller, &err);
	std::unique_ptr<char, decltype(&free)> prevCallerOwner(prevCaller, free);

	auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					 std::chrono::system_clock::now().time_since_epoch())
					 .count();
	const std::string directorUrl = "https://director.example.org:8443";
	const std::string originUrl = "https://origin.example.org:8440";
	const std::string childLotName = "origin-owned-child";

	// 0. lotman requires a `default` lot to exist before any other can be
	//    added. Other tests in this suite (e.g. GetTotalUsageBTest) create
	//    it as part of their own setup, but we make this test self-sufficient
	//    so the suite's filter ordering can't mask the regression. Both the
	//    add and the chosen caller below are no-ops when default already
	//    exists.
	(void)lotman_set_context_str("caller", "owner2", &err);
	json defaultJSON = createLotJSON(
		"default", "owner2", "/default", true, 1.0, 1.0, nowMs - 1000,
		nowMs + (365LL * 24 * 3600 * 1000),
		nowMs + (2LL * 365 * 24 * 3600 * 1000));
	(void)lotman_add_lot(defaultJSON.dump().c_str(), &err);

	// 1. Director bootstraps the root lot.
	(void)lotman_set_context_str("caller", directorUrl.c_str(), &err);
	json rootJSON =
		createLotJSON("root", directorUrl, "/", false, 1.0, 1.0,
					  nowMs - 1000, nowMs + (365LL * 24 * 3600 * 1000),
					  nowMs + (2LL * 365 * 24 * 3600 * 1000));
	(void)lotman_add_lot(rootJSON.dump().c_str(), &err);

	// 2. Director creates an origin-owned child lot under root. lotman's
	//    add_lot authorisation checks caller against the *parent* lot's
	//    owners (not against the new lot's own owner field), so the
	//    director — who owns root — is allowed to register a child whose
	//    recorded owner is a different identity. This mirrors how Pelican's
	//    director provisions per-origin lots on behalf of origins.
	json childJSON = {{"lot_name", childLotName},
					  {"owner", originUrl},
					  {"parents", {"root"}},
					  {"paths",
					   {{{"path", "/origin-owned-child"},
						 {"recursive", true}}}},
					  {"management_policy_attrs",
					   {{"dedicated_GB", 0.001},
						{"opportunistic_GB", 0.001},
						{"max_num_objects", 100},
						{"creation_time", nowMs - (3600LL * 1000)},
						{"expiration_time", nowMs - (60LL * 1000)},
						{"deletion_time", nowMs + (3600LL * 1000)}}}};
	ASSERT_EQ(lotman_add_lot(childJSON.dump().c_str(), &err), 0)
		<< (err ? err : "(null)");

	// 3. Simulate the cache being a wholly separate process by wiping the
	//    caller context: we must not leak the director identity that
	//    happened to be installed for setup. The post-fix ConfigPurgePin is
	//    the only thing allowed to re-install caller below.
	(void)lotman_set_context_str("caller", "owner1", &err);

	// 3. Cache plugin installs its caller context via ConfigPurgePin.
	std::string lotHome = LMSetupTeardown::tmp_dir;
	XrdPurgeLotManTest testPurgePin{"trace-log-level.cfg"};
	ASSERT_TRUE(testPurgePin.ConfigPurgePin(lotHome.c_str()));

	// 4. Cache reclaims the origin-owned child lot. Before the fix this
	//    returned LOTMAN_RECLAIM_ERROR with "Caller does not have proper
	//    ownership". After the fix, caller is the director URL (root's
	//    owner) which authorises the cascade because root is in every
	//    descendant's recursive parent chain.
	int rv = lotman_reclaim_lot(childLotName.c_str(),
								static_cast<int64_t>(nowMs), "exp", &err);
	EXPECT_EQ(rv, LOTMAN_RECLAIM_OK)
		<< "Reclaim of " << childLotName
		<< " must succeed when caller is root lot's owner. err: "
		<< (err ? err : "(null)");

	// Cleanup so the suite-shared db doesn't trip later tests. Removal,
	// like reclaim, runs against the parent-chain check — act as the
	// director here so we're authorised against `root`.
	(void)lotman_set_context_str("caller", directorUrl.c_str(), &err);
	(void)lotman_remove_lot(childLotName.c_str(), true, true, false, false,
							&err);

	const char *toRestore = prevCaller ? prevCaller : "owner1";
	(void)lotman_set_context_str("caller", toRestore, &err);
}

// Smoke test for the new reclamation ledger API. The complete-purge code path
// in completePurgePolicyBase calls lotman_reclaim_lot after scheduling drains;
// this test exercises the bare API to confirm:
//   1. Reclaiming an active lot returns OK on first call.
//   2. A second reclaim of the same lot returns ALREADY_RECLAIMED, not an
//      error — so successive purge ticks won't false-alarm.
//   3. After reclamation, get_lots_past_exp(include_reclaimed=false) no longer
//      lists the lot, but include_reclaimed=true still does.
TEST_F(LMSetupTeardown, ReclaimLotIsIdempotentAndFiltersFromGetLotsPastExp) {
	// lotman context is process-global; the prior test
	// (ConfigPurgePinSetsRootCallerContext) may have left caller="root".
	// Force caller to "owner1" so the lot we add here is owned by an
	// identity authorised to reclaim it.
	char *err = nullptr;
	ASSERT_EQ(lotman_set_context_str("caller", "owner1", &err), 0)
		<< (err ? err : "(null)");

	auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					 std::chrono::system_clock::now().time_since_epoch())
					 .count();

	// Lot already expired (expiration_time in the past) so it shows up in
	// get_lots_past_exp before reclaim.
	json lotJSON = createLotJSON("reclaim-test", "owner1", "/reclaim-test",
								 true, 0.001, 0.001,
								 nowMs - (3600LL * 1000), // creation 1h ago
								 nowMs - (60LL * 1000),	  // expired 1m ago
								 nowMs + (3600LL * 1000)); // not yet deletable
	std::string lotStr = lotJSON.dump();
	ASSERT_EQ(lotman_add_lot(lotStr.c_str(), &err), 0)
		<< (err ? err : "(null)");

	// Pre-condition: lot is visible in get_lots_past_exp.
	{
		char **lots = nullptr;
		ASSERT_EQ(lotman_get_lots_past_exp(static_cast<int64_t>(nowMs), true,
										   false, &lots, &err),
				  0)
			<< (err ? err : "(null)");
		bool found = false;
		for (int i = 0; lots && lots[i]; ++i) {
			if (std::string(lots[i]) == "reclaim-test") {
				found = true;
				break;
			}
		}
		lotman_free_string_list(lots);
		EXPECT_TRUE(found)
			<< "expected reclaim-test in get_lots_past_exp before reclaim";
	}

	// First reclaim: OK.
	int rv = lotman_reclaim_lot("reclaim-test", static_cast<int64_t>(nowMs),
								"exp", &err);
	EXPECT_EQ(rv, LOTMAN_RECLAIM_OK) << (err ? err : "(null)");

	// Second reclaim: ALREADY_RECLAIMED, not ERROR. This is the property the
	// purge plugin relies on: re-running policy on a transient
	// post-reclaim/pre-prune window must not emit error noise.
	rv = lotman_reclaim_lot("reclaim-test", static_cast<int64_t>(nowMs + 1),
							"exp", &err);
	EXPECT_EQ(rv, LOTMAN_RECLAIM_ALREADY_RECLAIMED)
		<< (err ? err : "(null)");

	// Post-condition: include_reclaimed=false hides it; include_reclaimed=true
	// still shows it (forensics window).
	{
		char **lots = nullptr;
		ASSERT_EQ(lotman_get_lots_past_exp(static_cast<int64_t>(nowMs + 1000),
										   true, false, &lots, &err),
				  0)
			<< (err ? err : "(null)");
		for (int i = 0; lots && lots[i]; ++i) {
			EXPECT_STRNE(lots[i], "reclaim-test")
				<< "reclaimed lot should be filtered";
		}
		lotman_free_string_list(lots);
	}
	{
		char **lots = nullptr;
		ASSERT_EQ(lotman_get_lots_past_exp(static_cast<int64_t>(nowMs + 1000),
										   true, true, &lots, &err),
				  0)
			<< (err ? err : "(null)");
		bool found = false;
		for (int i = 0; lots && lots[i]; ++i) {
			if (std::string(lots[i]) == "reclaim-test") {
				found = true;
				break;
			}
		}
		lotman_free_string_list(lots);
		EXPECT_TRUE(found)
			<< "include_reclaimed=true should still surface reclaimed lot";
	}

	// Cleanup — remove_lot lets later tests re-use a clean slate even though
	// SetUpTestSuite already tore the dir.
	(void)lotman_remove_lot("reclaim-test", true, true, false, false, &err);
}

// Regression test for the spurious "Error finding usage for directory" log
// noise emitted on every purge tick. The underlying condition is benign:
// lotman knows about a path because a lot owns it, but xrootd's per-directory
// state vector has no node for the path because no files have been cached
// there yet (or the directory was just pruned). lotPerDirUsageB must:
//   1. Not abort or throw — just skip the path.
//   2. Not include the missing path in the returned usage map (zero bytes
//      recoverable would be misleading).
//   3. Still include any paths that DO have DirState entries, with their
//      correct byte counts derived from m_StBlocks * 512.
TEST_F(LMSetupTeardown,
	   LotPerDirUsageSkipsPathsWithNoDirStateNodeAndKeepsKnownOnes) {
	char *err = nullptr;
	ASSERT_EQ(lotman_set_context_str("caller", "owner1", &err), 0)
		<< (err ? err : "(null)");

	auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					 std::chrono::system_clock::now().time_since_epoch())
					 .count();

	// lotman requires the "default" lot to exist before any other lot can
	// be created. The fixture is per-suite (SetUpTestSuite, not SetUp), so
	// a prior test in this suite may have already created it; tolerate
	// either case rather than failing on a benign "lot exists" return.
	json defaultJSON = createLotJSON(
		"default", "owner1", "/default", true, 1.0, 1.0, nowMs,
		nowMs + (3600LL * 1000), nowMs + (7200LL * 1000));
	(void)lotman_add_lot(defaultJSON.dump().c_str(), &err);
	if (err) {
		free(err);
		err = nullptr;
	}

	// Lot owns two paths: /known and /unknown. We will populate purge_shot
	// with a DirState node for /known only.
	const std::string lotName = "per-dir-usage-test";
	json lotJSON = {
		{"lot_name", lotName},
		{"owner", "owner1"},
		{"parents", {lotName}},
		{"paths",
		 {{{"path", "/known"}, {"recursive", true}},
		  {{"path", "/unknown"}, {"recursive", true}}}},
		{"management_policy_attrs",
		 {{"dedicated_GB", 1.0},
		  {"opportunistic_GB", 1.0},
		  {"max_num_objects", 100},
		  {"creation_time", nowMs - 1000},
		  {"expiration_time", nowMs + (3600LL * 1000)},
		  {"deletion_time", nowMs + (7200LL * 1000)}}}};
	ASSERT_EQ(lotman_add_lot(lotJSON.dump().c_str(), &err), 0)
		<< (err ? err : "(null)");

	// Build a purge_shot tree containing only the root and /known. The
	// `/unknown` directory is intentionally absent — that's the bug
	// trigger.
	XrdPfc::DataFsPurgeshot purge_shot;
	XrdPfc::DirPurgeElement rootElement, knownElement;
	populatePurgeElement(rootElement, "", -1, 1, 2);
	populatePurgeElement(knownElement, "known", 0, 0, 0);
	// 1 GiB of usage on /known (in 512-byte blocks).
	const long long knownStBlocks = (1LL << 30) / 512;
	knownElement.m_usage.m_StBlocks = knownStBlocks;
	purge_shot.m_dir_vec.push_back(rootElement);
	purge_shot.m_dir_vec.push_back(knownElement);

	XrdPurgeLotManTest testPurgePin;
	auto usage = testPurgePin.testLotPerDirUsageB(lotName, purge_shot);

	// lotman_get_lot_dirs returns paths with a trailing slash (e.g.
	// "/known/"), and lotPerDirUsageB uses that string verbatim as the
	// map key. /known landed in the map with the expected byte count.
	auto knownIt = usage.find("/known/");
	ASSERT_NE(knownIt, usage.end())
		<< "/known/ has a DirState node and must appear in the usage map";
	EXPECT_EQ(knownIt->second, knownStBlocks * 512);

	// /unknown was silently skipped: not in the map, no exception.
	EXPECT_EQ(usage.find("/unknown/"), usage.end())
		<< "/unknown/ has no DirState node; lotPerDirUsageB must omit it "
		   "(otherwise we'd attribute fabricated zero bytes to a "
		   "non-existent directory)";

	// Cleanup
	(void)lotman_remove_lot(lotName.c_str(), true, true, false, false, &err);
}

// Main test runner
int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
