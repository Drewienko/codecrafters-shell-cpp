#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class PathResolver
{
public:
  bool refresh();
  std::optional<std::string> findExecutable(const std::string &name) const;
  void forEachExecutable(const std::function<void(const std::filesystem::path &)> &callback) const;

private:
  std::string cachedPathValue{};
  std::vector<std::filesystem::path> cachedDirs{};

  static std::vector<std::filesystem::path> splitPathValue(const std::string &pathValue);
  static bool isExecutable(const std::filesystem::path &path);
};
