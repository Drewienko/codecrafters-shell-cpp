#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "trie.hpp"

class Shell
{
public:
  Shell(int argc, char *argvInput[], char **envpInput);
  void run();
  std::vector<std::string> argv, envp;

  ~Shell() = default;
  Shell(const Shell &) = default;
  Shell &operator=(const Shell &) = default;
  Shell(Shell &&) noexcept = default;
  Shell &operator=(Shell &&) noexcept = default;

private:
  struct OutputRedirection
  {
    bool enabled{false};
    bool append{false};
    std::string file{};
  };

  struct ParsedCommand
  {
    std::vector<std::string> args{};
    OutputRedirection stdoutRedir{};
    OutputRedirection stderrRedir{};
  };

  static inline Shell *activeShell{nullptr};

  using CommandHandler = std::function<int(const std::vector<std::string> &)>;

  std::unordered_map<std::string, CommandHandler> commands;
  Trie completionTrie{};
  bool pendingCompletionList{false};
  std::string pendingCompletionLine{};
  std::size_t pendingCompletionPoint{0};
  static constexpr std::size_t completionQueryItems{100};
  std::string cachedPathValue{};
  int historyAppendedCount{0};
  int mainPid{};

  void registerBuiltin(const std::string &name, CommandHandler handler);
  std::vector<std::string> tokenize(const std::string &line) const;
  static int handleTab(int count, int key);
  int runCommand(const std::vector<std::string> &parts);
  void loadPathExecutables();
  void resetCompletionState();
  bool parseCommandTokens(const std::vector<std::string> &parts, ParsedCommand &command, bool allowEmpty);
  std::vector<std::vector<std::string>> splitPipeline(const std::vector<std::string> &parts) const;
  int runSingleCommand(const ParsedCommand &command);
  int runPipeline(const std::vector<ParsedCommand> &commands);
  int runType(const std::vector<std::string> &args) const;
  int runPwd();
  int runCd(const std::vector<std::string> &args);
  int runHistory(const std::vector<std::string> &args);
  void loadHistoryFromEnv();
  void saveHistoryToEnv();
  bool loadHistoryFromFile(const std::string &path);
  std::filesystem::path sanitizePath(const std::string &path) const;
  std::optional<std::string> getEnvValue(const std::string &key) const;
  void setEnvValue(const std::string &key, const std::string &value);
  std::optional<std::string> getCurrentDir() const;
  std::optional<std::string> findExecutable(const std::string &name) const;
  bool isExecutable(const std::filesystem::path &path) const;
  std::vector<char *> argvHelper(const std::vector<std::string> &parts);
  int externalCommand(const std::string &path,
                      const std::vector<std::string> &parts,
                      const OutputRedirection &stdoutRedir,
                      const OutputRedirection &stderrRedir);
};
