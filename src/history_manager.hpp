#pragma once

#include <optional>
#include <string>
#include <vector>

class HistoryManager
{
public:
  explicit HistoryManager(int mainPid);
  void loadFromEnv();
  void saveToEnv();
  void addEntry(const std::string &line);
  int runHistory(const std::vector<std::string> &args);

private:
  int historyAppendedCount{0};
  int mainPid{};

  std::optional<int> handleOption(const std::vector<std::string> &args);
  std::optional<int> parseLimit(const std::vector<std::string> &args) const;
  void printHistory(int limit);
  int readHistoryFromPath(const std::string &path);
  int writeHistoryToPath(const std::string &path);
  int appendHistoryToPath(const std::string &path);
  bool loadHistoryFromFile(const std::string &path);
  std::optional<std::string> resolveHistoryPath(const std::vector<std::string> &args,
                                                std::size_t pathIndex,
                                                const std::string &option) const;
};
