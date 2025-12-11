#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Shell
{
public:
  Shell();
  void run();

private:
  using CommandHandler = std::function<int(const std::vector<std::string> &)>;

  std::unordered_map<std::string, CommandHandler> commands;

  void registerBuiltin(const std::string &name, CommandHandler handler);
  std::vector<std::string> tokenize(const std::string &line) const;
  int runCommand(const std::vector<std::string> &parts);
  int runType(const std::vector<std::string> &args) const;
};
