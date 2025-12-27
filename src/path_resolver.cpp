#include "path_resolver.hpp"

#include <cstdlib>
#include <system_error>

#include "path_utils.hpp"

bool PathResolver::refresh()
{
  const char *pathEnv{std::getenv("PATH")};
  const std::string pathValue{pathEnv ? pathEnv : ""};
  if (pathValue == cachedPathValue)
    return false;

  cachedPathValue = pathValue;
  cachedDirs = splitPathValue(pathValue);
  return true;
}

std::optional<std::string> PathResolver::findExecutable(const std::string &name) const
{
  if (name.empty())
    return std::nullopt;

  for (const auto &dirPath : cachedDirs)
  {
    const std::filesystem::path candidate{dirPath / name};
    if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate) && isExecutable(candidate))
      return candidate.string();
  }

  return std::nullopt;
}

void PathResolver::forEachExecutable(const std::function<void(const std::filesystem::path &)> &callback) const
{
  for (const auto &dirPath : cachedDirs)
  {
    std::error_code ec{};
    std::filesystem::directory_iterator it{dirPath, ec};
    if (ec)
      continue;

    for (const auto &entry : it)
    {
      if (!entry.is_regular_file(ec))
      {
        if (ec)
          ec.clear();
        continue;
      }

      const auto &path{entry.path()};
      if (isExecutable(path))
        callback(path);
    }
  }
}

std::vector<std::filesystem::path> PathResolver::splitPathValue(const std::string &pathValue)
{
  std::vector<std::filesystem::path> dirs{};
  std::string segment{};

  auto pushSegment{[&]()
                   {
                     if (segment.empty())
                       return;
                     dirs.push_back(normalizePath(segment));
                     segment.clear();
                   }};

  for (char c : pathValue)
  {
    if (c == ':' || c == ';')
    {
      pushSegment();
      continue;
    }
    segment.push_back(c);
  }

  pushSegment();
  return dirs;
}

bool PathResolver::isExecutable(const std::filesystem::path &path)
{
  std::error_code ec{};
  const auto perms{std::filesystem::status(path, ec).permissions()};
  if (ec)
    return false;

  using std::filesystem::perms;
  const auto mask{perms::owner_exec | perms::group_exec | perms::others_exec};
  return (perms::none != (perms & mask));
}
