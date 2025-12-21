#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Shell
{
public:
  Shell(int argc, char *argv[], char **envp);
  void run();
  std::vector<std::string> argv, envp;

  ~Shell() = default;
  Shell &operator=(const Shell &&other) = delete;

private:
  struct OutputRedirection
  {
    bool enabled = false;
    bool append = false;
    std::string file;
  };

  using CommandHandler = std::function<int(const std::vector<std::string> &)>;

  std::unordered_map<std::string, CommandHandler> commands;

  void registerBuiltin(const std::string &name, CommandHandler handler);
  std::vector<std::string> tokenize(const std::string &line) const;
  int runCommand(const std::vector<std::string> &parts);
  int runType(const std::vector<std::string> &args) const;
  int runPwd();
  int runCd(const std::vector<std::string> &args);
  std::optional<std::string> getEnvValue(const std::string &key) const;
  void setEnvValue(const std::string &key, const std::string &value);
  std::optional<std::string> getCurrentDir() const;
  std::optional<std::string> findExecutable(const std::string &name) const;
  bool isExecutable(const std::filesystem::path &path) const;
  std::vector<char *> argvHelper(const std::vector<std::string> &parts);
  int externalCommand(const std::string &path, const std::vector<std::string> &parts, const OutputRedirection &redir);
};
