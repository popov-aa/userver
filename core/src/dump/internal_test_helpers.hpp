#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <dump/config.hpp>
#include <fs/blocking/temp_directory.hpp>
#include <utest/utest.hpp>

// Note: cpp with the implementation is named "internal_helpers_test.cpp"

namespace dump {

Config ConfigFromYaml(const std::string& yaml_string,
                      const fs::blocking::TempDirectory& dump_root,
                      std::string_view dumper_name);

/// Create files, writing their own filenames into them
void CreateDumps(const std::vector<std::string>& filenames,
                 const fs::blocking::TempDirectory& dump_root,
                 std::string_view dumper_name);

void CreateDump(std::string_view contents, const Config& config);

/// @note Returns filenames, not full paths
std::set<std::string> FilenamesInDirectory(
    const fs::blocking::TempDirectory& dump_root, std::string_view dumper_name);

}  // namespace dump
