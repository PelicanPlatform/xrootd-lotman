#include "../src/XrdPurgeLotMan.hh"
#include <XrdPfc/XrdPfc.hh>
#include <filesystem>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

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
	XrdPfc::DirPurgeElement rootElement;
	rootElement.m_dir_name = "dir";
	rootElement.m_daughters_begin = 1;
	rootElement.m_daughters_end = 3;

	XrdPfc::DirPurgeElement subElement1;
	subElement1.m_dir_name = "subdir1";
	subElement1.m_daughters_begin = 0;
	subElement1.m_daughters_end = 0;

	XrdPfc::DirPurgeElement subElement2;
	subElement2.m_dir_name = "subdir2";
	subElement2.m_daughters_begin = 3;
	subElement2.m_daughters_end = 4;

	XrdPfc::DirPurgeElement subElement3;
	subElement3.m_dir_name = "subdir3";
	subElement3.m_daughters_begin = 0;
	subElement3.m_daughters_end = 0;

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
	XrdPfc::DirPurgeElement rootElement;
	rootElement.m_dir_name = "";
	rootElement.m_parent = -1;

	XrdPfc::DirPurgeElement parentElement;
	parentElement.m_dir_name = "dir";
	parentElement.m_daughters_begin = 1;
	parentElement.m_daughters_end = 3;
	parentElement.m_parent = 0;

	XrdPfc::DirPurgeElement subElement1;
	subElement1.m_dir_name = "subdir1";
	subElement1.m_daughters_begin = 0;
	subElement1.m_daughters_end = 0;
	subElement1.m_parent = 1;

	XrdPfc::DirPurgeElement subElement2;
	subElement2.m_dir_name = "subdir2";
	subElement2.m_daughters_begin = 3;
	subElement2.m_daughters_end = 4;
	subElement2.m_parent = 1;

	XrdPfc::DirPurgeElement subElement3;
	subElement3.m_dir_name = "subdir3";
	subElement3.m_daughters_begin = 0;
	subElement3.m_daughters_end = 0;
	subElement3.m_parent = 3;

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

// Main test runner
int main(int argc, char **argv) {
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
