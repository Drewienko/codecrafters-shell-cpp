#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "command.hpp"
#include "completion_engine.hpp"
#include "history_manager.hpp"
#include "pipeline_executor.hpp"
#include "path_resolver.hpp"
#include "tokenizer.hpp"

class Shell
{
public:
  Shell(int argc, char *argvInput[], char **envpInput);
  void run();

  ~Shell() = default;
  Shell(const Shell &) = delete;
  Shell &operator=(const Shell &) = delete;
  Shell(Shell &&) noexcept = delete;
  Shell &operator=(Shell &&) noexcept = delete;

private:
  using CommandHandler = std::function<int(const std::vector<std::string> &)>;

  std::vector<std::string> argv{};
  std::vector<std::string> envp{};
  std::unordered_map<std::string, CommandHandler> commands;
  PathResolver pathResolver{};
  CompletionEngine completionEngine;
  PipelineExecutor pipelineExecutor{};
  Tokenizer tokenizer{};
  HistoryManager historyManager;

  void registerBuiltin(const std::string &name, CommandHandler handler);
  int runCommand(const std::vector<std::string> &parts);
  bool parseCommandTokens(const std::vector<std::string> &parts, ParsedCommand &command, bool allowEmpty);
  std::vector<std::vector<std::string>> splitPipeline(const std::vector<std::string> &parts) const;
  int executeCommand(const ParsedCommand &command, ExecMode mode);
  int runPipeline(const std::vector<ParsedCommand> &commands);
  int runType(const std::vector<std::string> &args);
  int runPwd();
  int runCd(const std::vector<std::string> &args);
  int openRedirectionFile(const OutputRedirection &redir) const;
  bool applyRedirection(const OutputRedirection &redir, int targetFd, int *savedFd);
  void restoreFd(int targetFd, int &savedFd);
  std::optional<std::string> getEnvValue(const std::string &key) const;
  void setEnvValue(const std::string &key, const std::string &value);
  std::optional<std::string> getCurrentDir() const;
  std::optional<std::string> findExecutable(const std::string &name);
  std::vector<char *> argvHelper(const std::vector<std::string> &parts);
  int execExternal(const std::string &path,
                   const std::vector<std::string> &parts,
                   const OutputRedirection &stdoutRedir,
                   const OutputRedirection &stderrRedir);
  int externalCommand(const std::string &path,
                      const std::vector<std::string> &parts,
                      const OutputRedirection &stdoutRedir,
                      const OutputRedirection &stderrRedir);
};
