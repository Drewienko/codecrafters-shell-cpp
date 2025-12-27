#include "shell.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <memory>
#include <readline/readline.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

#include "fd_utils.hpp"
#include "path_utils.hpp"

Shell::Shell(int argc, char *argvInput[], char **envpInput)
    : historyManager{static_cast<int>(::getpid())}
{
  this->argv.reserve(static_cast<std::size_t>(argc));
  std::transform(argvInput, argvInput + argc, std::back_inserter(this->argv),
                 [](char *arg)
                 { return std::string{arg ? arg : ""}; });

  if (envpInput)
  {
    char **envEnd{envpInput};
    while (*envEnd)
      ++envEnd;
    this->envp.reserve(static_cast<std::size_t>(envEnd - envpInput));
    std::transform(envpInput, envEnd, std::back_inserter(this->envp),
                   [](char *entry)
                   { return std::string{entry ? entry : ""}; });
  }

  historyManager.loadFromEnv();

  registerBuiltin("exit", [this](const auto &)
                  {
    historyManager.saveToEnv();
    std::exit(0);
    return 0; });

  registerBuiltin("echo", [](const auto &args)
                  {
    for (std::size_t i{1}; i < args.size(); ++i)
    {
      if (i > 1)
        std::cout << ' ';
      std::cout << args[i];
    }
    std::cout << "\n";
    return 0; });

  registerBuiltin("type", [this](const auto &args)
                  { return runType(args); });

  registerBuiltin("pwd", [this](const auto &)
                  { return runPwd(); });

  registerBuiltin("cd", [this](const auto &args)
                  { return runCd(args); });

  registerBuiltin("history", [this](const auto &args)
                  { return historyManager.runHistory(args); });

  completionEngine.refreshExecutables();
}

void Shell::registerBuiltin(const std::string &name, CommandHandler handler)
{
  commands[name] = std::move(handler);
  completionEngine.registerBuiltin(name);
}

int Shell::openRedirectionFile(const OutputRedirection &redir) const
{
  const std::string targetPath{normalizePath(redir.file).string()};
  return open(targetPath.c_str(),
              O_WRONLY | O_CREAT | (redir.append ? O_APPEND : O_TRUNC),
              0644);
}

bool Shell::applyRedirection(const OutputRedirection &redir, int targetFd, int *savedFd)
{
  if (!redir.enabled)
    return true;

  if (savedFd)
  {
    *savedFd = dup(targetFd);
    if (*savedFd < 0)
    {
      perror("dup");
      return false;
    }
  }

  UniqueFd fileFd{openRedirectionFile(redir)};
  if (!fileFd)
  {
    perror("open");
    if (savedFd)
    {
      close(*savedFd);
      *savedFd = -1;
    }
    return false;
  }

  if (dup2(fileFd.get(), targetFd) < 0)
  {
    perror("dup2");
    if (savedFd)
    {
      close(*savedFd);
      *savedFd = -1;
    }
    return false;
  }
  return true;
}

void Shell::restoreFd(int targetFd, int &savedFd)
{
  if (savedFd < 0)
    return;
  if (dup2(savedFd, targetFd) < 0)
    perror("dup2");
  close(savedFd);
  savedFd = -1;
}

bool Shell::parseCommandTokens(const std::vector<std::string> &parts, ParsedCommand &command, bool allowEmpty)
{
  command = ParsedCommand{};
  command.args.reserve(parts.size());
  for (std::size_t i{}; i < parts.size(); ++i)
  {
    const std::string &token{parts[i]};
    bool append{false};
    OutputRedirection *target{nullptr};

    if (token == ">" || token == "1>")
      target = &command.stdoutRedir;
    else if (token == ">>" || token == "1>>")
    {
      target = &command.stdoutRedir;
      append = true;
    }
    else if (token == "2>")
    {
      target = &command.stderrRedir;
    }
    else if (token == "2>>")
    {
      target = &command.stderrRedir;
      append = true;
    }

    if (target)
    {
      if (i + 1 >= parts.size())
      {
        std::cerr << "syntax error: missing file for redirection\n";
        return false;
      }
      target->enabled = true;
      target->append = append;
      target->file = parts[i + 1];
      ++i;
      continue;
    }

    command.args.push_back(token);
  }

  if (command.args.empty())
  {
    if (allowEmpty)
      return true;
    std::cerr << "syntax error: missing command\n";
    return false;
  }

  return true;
}

std::vector<std::vector<std::string>> Shell::splitPipeline(const std::vector<std::string> &parts) const
{
  std::vector<std::vector<std::string>> segments{};
  segments.emplace_back();
  for (const auto &token : parts)
  {
    if (token == "|")
    {
      segments.emplace_back();
      continue;
    }
    segments.back().push_back(token);
  }
  return segments;
}

int Shell::executeCommand(const ParsedCommand &command, ExecMode mode)
{
  if (command.args.empty())
    return 0;

  const auto cmd{commands.find(command.args[0])};
  if (cmd != commands.end())
  {
    if (mode == ExecMode::Parent && !command.stdoutRedir.enabled && !command.stderrRedir.enabled)
      return cmd->second(command.args);

    int savedStdout{-1};
    int savedStderr{-1};
    int *savedStdoutPtr{mode == ExecMode::Parent ? &savedStdout : nullptr};
    int *savedStderrPtr{mode == ExecMode::Parent ? &savedStderr : nullptr};

    if (!applyRedirection(command.stdoutRedir, STDOUT_FILENO, savedStdoutPtr))
      return 1;
    if (!applyRedirection(command.stderrRedir, STDERR_FILENO, savedStderrPtr))
    {
      if (mode == ExecMode::Parent)
        restoreFd(STDOUT_FILENO, savedStdout);
      return 1;
    }

    int rc{cmd->second(command.args)};
    if (mode == ExecMode::Parent)
    {
      restoreFd(STDERR_FILENO, savedStderr);
      restoreFd(STDOUT_FILENO, savedStdout);
    }
    return rc;
  }

  auto path{findExecutable(command.args[0])};
  if (path)
  {
    if (mode == ExecMode::Parent)
      return externalCommand(*path, command.args, command.stdoutRedir, command.stderrRedir);
    return execExternal(*path, command.args, command.stdoutRedir, command.stderrRedir);
  }

  std::cerr << command.args[0] << ": command not found\n";
  return 127;
}

int Shell::runPipeline(const std::vector<ParsedCommand> &commands)
{
  return pipelineExecutor.run(commands,
                              [this](const ParsedCommand &command, ExecMode mode)
                              { return executeCommand(command, mode); });
}

int Shell::runCommand(const std::vector<std::string> &parts)
{
  if (parts.empty())
    return 0;

  auto segments{splitPipeline(parts)};
  if (segments.size() > 1)
  {
    std::vector<ParsedCommand> parsed{};
    parsed.reserve(segments.size());
    for (const auto &segment : segments)
    {
      ParsedCommand command{};
      if (!parseCommandTokens(segment, command, false))
        return 1;
      parsed.push_back(std::move(command));
    }
    return runPipeline(parsed);
  }

  ParsedCommand command{};
  if (!parseCommandTokens(parts, command, true))
    return 1;
  return executeCommand(command, ExecMode::Parent);
}

std::vector<char *> Shell::argvHelper(const std::vector<std::string> &parts)
{
  std::vector<char *> execArgv{};
  execArgv.reserve(parts.size() + 1);
  std::transform(parts.begin(), parts.end(), std::back_inserter(execArgv),
                 [](const std::string &part)
                 { return const_cast<char *>(part.c_str()); });
  execArgv.push_back(nullptr);

  return execArgv;
}

int Shell::execExternal(const std::string &path,
                        const std::vector<std::string> &parts,
                        const OutputRedirection &stdoutRedir,
                        const OutputRedirection &stderrRedir)
{
  if (!applyRedirection(stdoutRedir, STDOUT_FILENO, nullptr))
    return 127;
  if (!applyRedirection(stderrRedir, STDERR_FILENO, nullptr))
    return 127;

  std::vector<char *> execArgv{argvHelper(parts)};
  extern char **environ;
  execve(path.c_str(), execArgv.data(), environ);
  perror("execve");
  return 127;
}

int Shell::externalCommand(const std::string &path,
                           const std::vector<std::string> &parts,
                           const OutputRedirection &stdoutRedir,
                           const OutputRedirection &stderrRedir)
{

  pid_t pid{fork()};
  if (pid == 0)
  {
    int rc{execExternal(path, parts, stdoutRedir, stderrRedir)};
    _exit(rc);
  }
  else if (pid > 0)
  {
    int status{};
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
  }
  else
  {
    perror("fork");
    return 127;
  }
}

int Shell::runType(const std::vector<std::string> &args)
{
  if (args.size() < 2)
    return 0;

  for (std::size_t i{1}; i < args.size(); ++i)
  {
    const auto &name{args[i]};

    if (commands.find(name) != commands.end())
    {
      std::cout << name << " is a shell builtin\n";
      continue;
    }

    auto path{findExecutable(name)};
    if (path)
    {
      std::cout << name << " is " << *path << "\n";
      continue;
    }

    std::cout << name << ": not found\n";
  }

  return 0;
}

int Shell::runPwd()
{
  if (const char *pwd{std::getenv("PWD")}; pwd && *pwd)
  {
    std::cout << pwd << "\n";
    return 0;
  }

  if (auto pwd{getEnvValue("PWD")}; pwd)
  {
    std::cout << *pwd << "\n";
    return 0;
  }

  std::cerr << "pwd: PWD not set\n";
  return 1;
}

int Shell::runCd(const std::vector<std::string> &args)
{
  std::string target{};
  if (args.size() < 2)
  {
    const char *home{std::getenv("HOME")};
    if (!home || *home == '\0')
    {
      std::cerr << "cd: HOME not set\n";
      return 1;
    }
    target = home;
  }
  else
  {
    target = args[1];
  }

  if (!target.empty() && target[0] == '~')
  {
    if (target.size() == 1 || target[1] == '/')
    {
      const char *home{std::getenv("HOME")};
      if (!home || *home == '\0')
      {
        std::cerr << "cd: HOME not set\n";
        return 1;
      }
      target = std::string{home} + target.substr(1);
    }
  }

  std::optional<std::string> oldPwd{getCurrentDir()};
  if (!oldPwd)
    oldPwd = getEnvValue("PWD");

  const std::string normalizedTarget{normalizePath(target).string()};
  if (chdir(normalizedTarget.c_str()) != 0)
  {
    std::cerr << "cd: " << target << ": " << std::strerror(errno) << "\n";
    return 1;
  }

  std::string newPwd{};
  if (auto cwd{getCurrentDir()}; cwd)
    newPwd = *cwd;
  else
    newPwd = target;

  if (oldPwd)
    setEnvValue("OLDPWD", *oldPwd);
  setEnvValue("PWD", newPwd);
  return 0;
}

std::optional<std::string> Shell::findExecutable(const std::string &name)
{
  pathResolver.refresh();
  return pathResolver.findExecutable(name);
}

std::optional<std::string> Shell::getEnvValue(const std::string &key) const
{
  const std::string prefix{key + "="};
  auto it{std::find_if(envp.begin(), envp.end(),
                       [&prefix](const std::string &entry)
                       { return entry.compare(0, prefix.size(), prefix) == 0; })};
  if (it != envp.end())
    return it->substr(prefix.size());
  return std::nullopt;
}

void Shell::setEnvValue(const std::string &key, const std::string &value)
{
  const std::string prefix{key + "="};
  auto it{std::find_if(envp.begin(), envp.end(),
                       [&prefix](const std::string &entry)
                       { return entry.compare(0, prefix.size(), prefix) == 0; })};
  if (it != envp.end())
  {
    *it = prefix + value;
    setenv(key.c_str(), value.c_str(), 1);
    return;
  }

  envp.push_back(prefix + value);
  setenv(key.c_str(), value.c_str(), 1);
}

std::optional<std::string> Shell::getCurrentDir() const
{
  std::unique_ptr<char, decltype(&std::free)> cwd{::getcwd(nullptr, 0), &std::free};
  if (!cwd)
    return std::nullopt;

  std::string result{cwd.get()};
  return result;
}

void Shell::run()
{
  CompletionEngine::ActiveGuard completionGuard{completionEngine};
  rl_initialize();
  rl_bind_key('\t', &CompletionEngine::handleTab);

  std::string buffer{};
  bool awaitingContinuation{false};

  while (true)
  {
    const char *prompt{awaitingContinuation ? "> " : "$ "};
    std::unique_ptr<char, decltype(&std::free)> input{readline(prompt), &std::free};
    if (!input)
    {
      if (awaitingContinuation)
      {
        std::cerr << "syntax error: unexpected end of file\n";
        buffer.clear();
        awaitingContinuation = false;
        continue;
      }
      historyManager.saveToEnv();
      break;
    }

    std::string line{input.get()};

    if (!buffer.empty())
      buffer.push_back('\n');
    buffer += line;

    const auto parts{tokenizer.tokenize(buffer)};
    if (!parts.empty() && parts.back() == "|")
    {
      awaitingContinuation = true;
      continue;
    }

    awaitingContinuation = false;
    if (!buffer.empty())
      historyManager.addEntry(buffer);
    runCommand(parts);
    buffer.clear();
  }
}
