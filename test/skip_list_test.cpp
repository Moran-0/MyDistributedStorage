#include "skip_list.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        std::exit(1);
    }
}

void TestInsertSearch() {
    SkipList<int, std::string> skip_list(8);

    Check(skip_list.Size() == 0, "initial size should be 0");
    Check(skip_list.Insert(1, "one"), "insert key 1");
    Check(skip_list.Insert(2, "two"), "insert key 2");
    Check(!skip_list.Insert(2, "two-again"), "duplicate insert should fail");

    std::string value;
    Check(skip_list.Search(1, &value), "search key 1");
    Check(value == "one", "value of key 1 should be one");
    Check(skip_list.Search(2, &value), "search key 2");
    Check(value == "two", "value of key 2 should be two");
    Check(!skip_list.Search(3, &value), "search missing key should fail");
    Check(skip_list.Size() == 2, "size should be 2 after inserts");
}

void TestUpdateDelete() {
    SkipList<int, std::string> skip_list(8);
    skip_list.Insert(1, "one");
    skip_list.Insert(2, "two");

    Check(skip_list.Update(1, "ONE"), "update key 1");
    std::string value;
    Check(skip_list.Search(1, &value), "search updated key 1");
    Check(value == "ONE", "updated value of key 1 should be ONE");
    Check(!skip_list.Update(100, "none"), "update missing key should fail");

    Check(skip_list.Delete(1), "delete key 1");
    Check(!skip_list.Delete(1), "delete already deleted key should fail");
    Check(!skip_list.Search(1, &value), "deleted key should not be found");
    Check(skip_list.Size() == 1, "size should be 1 after delete");
}

void TestClearAndLoad() {
    SkipList<int, std::string> skip_list(16);
    for (int i = 0; i < 20; ++i) {
        Check(skip_list.Insert(i, std::to_string(i)), "bulk insert");
    }
    Check(skip_list.Size() == 20, "size should be 20 before dump");

    const std::string dump = skip_list.Dump();
    Check(!dump.empty(), "dump should not be empty");

    skip_list.Clear();
    Check(skip_list.Size() == 0, "size should be 0 after clear");
    std::string value;
    Check(!skip_list.Search(3, &value), "cleared list should not find old key");

    skip_list.Load(dump);
    Check(skip_list.Size() == 20, "size should be restored after load");
    for (int i = 0; i < 20; ++i) {
        Check(skip_list.Search(i, &value), "loaded key should be found");
        Check(value == std::to_string(i), "loaded value should match");
    }
}

void TestLoadOverwrite() {
    SkipList<int, std::string> skip_list(8);
    skip_list.Insert(1, "one");
    const std::string dump = skip_list.Dump();

    skip_list.Insert(2, "two");
    skip_list.Load(dump);

    std::string value;
    Check(skip_list.Search(1, &value), "loaded key 1 should exist");
    Check(value == "one", "loaded key 1 value should be one");
    Check(!skip_list.Search(2, &value), "load should overwrite old data");
    Check(skip_list.Size() == 1, "size should match restored data");
}

} // namespace

int main() {
    TestInsertSearch();
    TestUpdateDelete();
    TestClearAndLoad();
    TestLoadOverwrite();

    std::cout << "skip_list_test_passed" << std::endl;
    return 0;
}
