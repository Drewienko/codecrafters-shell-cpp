#include "history_manager.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <readline/history.h>
#include <unistd.h>

#include "path_utils.hpp"

HistoryManager::HistoryManager(int mainPid)
    : mainPid{mainPid}
{
  using_history();
}

void HistoryManager::loadFromEnv()
{
  const char *historyFile{std::getenv("HISTFILE")};
  if (!historyFile || *historyFile == '\0')
    return;

  loadHistoryFromFile(historyFile);
}

void HistoryManager::saveToEnv()
{
  if (static_cast<int>(::getpid()) != mainPid)
    return;

  const char *historyFile{std::getenv("HISTFILE")};
  if (!historyFile || *historyFile == '\0')
    return;

  const std::string normalizedPath{normalizePath(std::string{historyFile}).string()};
  if (write_history(normalizedPath.c_str()) == 0)
    historyAppendedCount = history_length;
}

void HistoryManager::addEntry(const std::string &line)
{
  if (line.empty())
    return;
  add_history(line.c_str());
}

int HistoryManager::runHistory(const std::vector<std::string> &args)
{
  if (auto result{handleOption(args)}; result)
    return *result;

  auto limit{parseLimit(args)};
  if (!limit)
    return 1;
  printHistory(*limit);
  return 0;
}

bool HistoryManager::loadHistoryFromFile(const std::string &path)
{
  std::ifstream file{normalizePath(path)};
  if (!file.is_open())
    return false;

  std::string line{};
  while (std::getline(file, line))
  {
    if (line.empty())
      continue;
    add_history(line.c_str());
  }

  historyAppendedCount = history_length;
  return true;
}

std::optional<int> HistoryManager::handleOption(const std::vector<std::string> &args)
{
  if (args.size() <= 1)
    return std::nullopt;

  const std::string &option{args[1]};
  if (option == "-r")
  {
    auto path{resolveHistoryPath(args, 2, option)};
    if (!path)
      return 1;
    return readHistoryFromPath(*path);
  }

  if (option == "-c")
  {
    clear_history();
    historyAppendedCount = 0;
    return 0;
  }

  if (option == "-w")
  {
    auto path{resolveHistoryPath(args, 2, option)};
    if (!path)
      return 1;
    return writeHistoryToPath(*path);
  }

  if (option == "-a")
  {
    auto path{resolveHistoryPath(args, 2, option)};
    if (!path)
      return 1;
    return appendHistoryToPath(*path);
  }

  if (!option.empty() && option[0] == '-')
  {
    std::cerr << "history: " << option << ": invalid option\n";
    return 1;
  }

  return std::nullopt;
}

std::optional<int> HistoryManager::parseLimit(const std::vector<std::string> &args) const
{
  if (args.size() <= 1)
    return -1;

  try
  {
    std::size_t consumed{};
    int requested{std::stoi(args[1], &consumed)};
    if (consumed == args[1].size() && requested >= 0)
      return requested;
  }
  catch (const std::exception &)
  {
  }

  std::cerr << "history: " << args[1] << ": numeric argument required\n";
  return std::nullopt;
}

void HistoryManager::printHistory(int limit)
{
  HIST_ENTRY **entries{history_list()};
  if (!entries)
    return;

  int entryCount{};
  while (entries[entryCount])
    ++entryCount;

  int startIndex{};
  if (limit >= 0 && limit < entryCount)
    startIndex = entryCount - limit;

  for (int i{startIndex}; i < entryCount; ++i)
  {
    const int index{history_base + i};
    const char *line{entries[i]->line ? entries[i]->line : ""};
    std::cout << std::setw(5) << index << "  " << line << "\n";
  }
}

int HistoryManager::readHistoryFromPath(const std::string &path)
{
  errno = 0;
  if (!loadHistoryFromFile(path))
  {
    std::cerr << "history: " << path << ": " << std::strerror(errno) << "\n";
    return 1;
  }
  return 0;
}

int HistoryManager::writeHistoryToPath(const std::string &path)
{
  const std::string normalizedPath{normalizePath(path).string()};
  if (write_history(normalizedPath.c_str()) != 0)
  {
    std::cerr << "history: " << path << ": " << std::strerror(errno) << "\n";
    return 1;
  }
  historyAppendedCount = history_length;
  return 0;
}

int HistoryManager::appendHistoryToPath(const std::string &path)
{
  int totalEntries{history_length};
  if (totalEntries < historyAppendedCount)
    historyAppendedCount = totalEntries;

  int newEntries{totalEntries - historyAppendedCount};
  if (newEntries <= 0)
    return 0;

  const std::string normalizedPath{normalizePath(path).string()};
  if (append_history(newEntries, normalizedPath.c_str()) != 0)
  {
    std::cerr << "history: " << path << ": " << std::strerror(errno) << "\n";
    return 1;
  }
  historyAppendedCount = totalEntries;
  return 0;
}

std::optional<std::string> HistoryManager::resolveHistoryPath(const std::vector<std::string> &args,
                                                              std::size_t pathIndex,
                                                              const std::string &option) const
{
  if (args.size() > pathIndex)
    return args[pathIndex];

  const char *historyFile{std::getenv("HISTFILE")};
  if (historyFile && *historyFile != '\0')
    return std::string{historyFile};

  std::cerr << "history: " << option << ": missing filename\n";
  return std::nullopt;
}
