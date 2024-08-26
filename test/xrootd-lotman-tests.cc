#include "../src/XrdPurgeLotMan.hh"

#include <XrdPfc/XrdPfc.hh>
#include <lotman/lotman.h>

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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
	XrdPurgeLotManTest() {}

	~XrdPurgeLotManTest() override = default;

	long long testGetTotalUsageB() { return getTotalUsageB(); }
	LotManConfiguration testGetLotmanConf() { return m_lotman_conf; }
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

	json result = dirNodeToJson(&node, purge_shot);

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

	json result = reconstructPathsAndBuildJson(purge_shot);

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
	rv = lotman_update_lot_usage_by_dir(usageUpdateCStr, false, &err);
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

	XrdPurgeLotManTest testPurgePin{};
	bool rv = testPurgePin.ConfigPurgePin(configParams.c_str());
	ASSERT_TRUE(rv);

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

// Main test runner
int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
