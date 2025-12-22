#include "shell.hpp"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <readline/history.h>
#include <readline/readline.h>
#include <system_error>
#include <utility>

Shell::Shell(int argc, char *argv[], char **envp)
{
  this->argv.reserve(static_cast<std::size_t>(argc));
  for (int i{}; i < argc; ++i)
    this->argv.emplace_back(argv[i] ? argv[i] : "");

  for (char **env{envp}; env && *env; ++env)
    this->envp.emplace_back(*env);

  registerBuiltin("exit", [](const auto &)
                  {
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

  loadPathExecutables();
}

void Shell::registerBuiltin(const std::string &name, CommandHandler handler)
{
  commands[name] = std::move(handler);
  completionTrie.insert(name, Trie::NodeKind::Builtin);
}

std::vector<std::string> Shell::tokenize(const std::string &line) const
{
  std::vector<std::string> parts{};
  std::string currentToken{};
  bool tokenStarted{false};

  enum class Mode
  {
    None,
    Single,
    Double
  };

  Mode mode{Mode::None};

  auto pushToken{[&]()
                 {
                   if (tokenStarted)
                     parts.push_back(currentToken);
                   currentToken.clear();
                   tokenStarted = false;
                 }};

  for (std::size_t i{}; i < line.size(); ++i)
  {
    char c{line[i]};

    switch (mode)
    {
    case Mode::Single:
      if (c == '\'')
      {
        mode = Mode::None;
        tokenStarted = true;
      }
      else
      {
        currentToken.push_back(c);
        tokenStarted = true;
      }
      break;
    case Mode::Double:
      if (c == '"')
      {
        mode = Mode::None;
        tokenStarted = true;
        break;
      }
      if (c == '\\' && i + 1 < line.size())
      {
        char next{line[i + 1]};
        if (next == '"' || next == '\\' || next == '$' || next == '`')
        {
          currentToken.push_back(next);
          ++i;
        }
        else
        {
          currentToken.push_back(c);
        }
        tokenStarted = true;
        break;
      }
      currentToken.push_back(c);
      tokenStarted = true;
      break;
    case Mode::None:
      if (c == '|')
      {
        pushToken();
        parts.push_back("|");
        break;
      }
      if (std::isspace(static_cast<unsigned char>(c)))
      {
        pushToken();
        break;
      }
      if (c == '\'')
      {
        mode = Mode::Single;
        tokenStarted = true;
        break;
      }
      if (c == '"')
      {
        mode = Mode::Double;
        tokenStarted = true;
        break;
      }
      if (c == '\\' && i + 1 < line.size())
      {
        currentToken.push_back(line[i + 1]);
        ++i;
        tokenStarted = true;
        break;
      }
      currentToken.push_back(c);
      tokenStarted = true;
      break;
    }
  }

  pushToken();
  return parts;
}

int Shell::handleTab(int, int)
{
  if (!activeShell)
    return 0;

  Shell *shell{activeShell};
  const char *buffer{rl_line_buffer};
  if (!buffer)
    return 0;

  std::string line{buffer};
  std::size_t point{static_cast<std::size_t>(rl_point)};
  if (point > line.size())
    point = line.size();

  std::size_t start{point};
  while (start > 0 && !std::isspace(static_cast<unsigned char>(line[start - 1])))
    --start;

  if (start != 0)
  {
    shell->resetCompletionState();
    return 0;
  }

  std::string prefix{line.substr(0, point)};
  if (prefix.empty())
  {
    shell->resetCompletionState();
    return 0;
  }

  shell->loadPathExecutables();
  auto matches{shell->completionTrie.collectWithPrefix(prefix)};
  if (matches.empty())
  {
    shell->resetCompletionState();
    return 0;
  }

  if (matches.size() == 1)
  {
    shell->resetCompletionState();
    const std::string &full{matches.front()};
    if (full.size() > prefix.size())
    {
      std::string suffix{full.substr(prefix.size())};
      suffix.push_back(' ');
      rl_insert_text(suffix.c_str());
    }
    else
    {
      rl_insert_text(" ");
    }
    rl_redisplay();
    ::write(STDOUT_FILENO, "\x07", 1);
    return 0;
  }

  std::string lcp{shell->completionTrie.longestCommonPrefix(prefix)};
  if (lcp.size() > prefix.size())
  {
    shell->resetCompletionState();
    std::string suffix{lcp.substr(prefix.size())};
    rl_insert_text(suffix.c_str());
    ::write(STDOUT_FILENO, "\x07", 1);
    rl_redisplay();

    ::write(STDOUT_FILENO, "\x07", 1);
    return 0;
  }

  if (shell->pendingCompletionList && shell->pendingCompletionLine == line && shell->pendingCompletionPoint == point)
  {
    shell->resetCompletionState();
    std::cout << "\n";
    bool shouldList{true};
    if (matches.size() > completionQueryItems)
    {
      std::cout << "Display all " << matches.size() << " possibilities? (y or n)\n";
      std::cout.flush();
      int choice{rl_read_key()};
      if (choice != 'y' && choice != 'Y')
        shouldList = false;
    }

    if (shouldList)
    {
      for (std::size_t i{}; i < matches.size(); ++i)
      {
        if (i > 0)
          std::cout << "  ";
        std::cout << matches[i];
      }
      std::cout << "\n";
    }

    rl_on_new_line();
    rl_redisplay();

    ::write(STDOUT_FILENO, "\x07", 1);
    return 0;
  }
  ::write(STDOUT_FILENO, "\x07", 1);
  shell->pendingCompletionList = true;
  shell->pendingCompletionLine = line;
  shell->pendingCompletionPoint = point;
  return 0;
}

void Shell::loadPathExecutables()
{
  const char *pathEnv{std::getenv("PATH")};
  if (!pathEnv)
    return;
  const std::string pathValue{pathEnv};
  if (pathValue == cachedPathValue)
    return;

  cachedPathValue = pathValue;
  completionTrie.clear();
  for (const auto &entry : commands)
    completionTrie.insert(entry.first, Trie::NodeKind::Builtin);

  std::string segment{};

  auto loadSegment{[&](const std::string &dir)
                   {
                     if (dir.empty())
                       return;

                     std::error_code ec{};
                     std::filesystem::directory_iterator it{dir, ec};
                     if (ec)
                       return;

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
                         completionTrie.insert(path.filename().string(), Trie::NodeKind::PathExecutable);
                     }
                   }};

  for (char c : pathValue)
  {
    if (c == ':' || c == ';')
    {
      loadSegment(segment);
      segment.clear();
      continue;
    }
    segment.push_back(c);
  }

  loadSegment(segment);
}

void Shell::resetCompletionState()
{
  pendingCompletionList = false;
  pendingCompletionLine.clear();
  pendingCompletionPoint = 0;
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

int Shell::runSingleCommand(const ParsedCommand &command)
{
  if (command.args.empty())
    return 0;

  const auto cmd{commands.find(command.args[0])};
  if (cmd != commands.end())
  {
    if (!command.stdoutRedir.enabled && !command.stderrRedir.enabled)
      return cmd->second(command.args);

    int savedStdout{-1};
    int savedStderr{-1};

    auto applyRedirection{[&](const OutputRedirection &redir, int targetFd, int &savedFd) -> bool
                          {
                            if (!redir.enabled)
                              return true;

                            savedFd = dup(targetFd);
                            if (savedFd < 0)
                            {
                              perror("dup");
                              return false;
                            }

                            int fd{open(redir.file.c_str(),
                                        O_WRONLY | O_CREAT | (redir.append ? O_APPEND : O_TRUNC),
                                        0644)};
                            if (fd < 0)
                            {
                              perror("open");
                              close(savedFd);
                              savedFd = -1;
                              return false;
                            }

                            if (dup2(fd, targetFd) < 0)
                            {
                              perror("dup2");
                              close(fd);
                              close(savedFd);
                              savedFd = -1;
                              return false;
                            }
                            close(fd);
                            return true;
                          }};

    auto restoreFd{[&](int targetFd, int &savedFd)
                   {
                     if (savedFd < 0)
                       return;
                     if (dup2(savedFd, targetFd) < 0)
                       perror("dup2");
                     close(savedFd);
                     savedFd = -1;
                   }};

    if (!applyRedirection(command.stdoutRedir, STDOUT_FILENO, savedStdout))
      return 1;
    if (!applyRedirection(command.stderrRedir, STDERR_FILENO, savedStderr))
    {
      restoreFd(STDOUT_FILENO, savedStdout);
      return 1;
    }

    int rc{cmd->second(command.args)};
    restoreFd(STDERR_FILENO, savedStderr);
    restoreFd(STDOUT_FILENO, savedStdout);
    return rc;
  }

  auto path{findExecutable(command.args[0])};
  if (path)
  {
    return externalCommand(*path, command.args, command.stdoutRedir, command.stderrRedir);
  }

  std::cerr << command.args[0] << ": command not found\n";
  return 127;
}

int Shell::runPipeline(const std::vector<ParsedCommand> &commands)
{
  if (commands.empty())
    return 0;

  std::vector<pid_t> pids{};
  pids.reserve(commands.size());

  int prevReadFd{-1};
  for (std::size_t i{}; i < commands.size(); ++i)
  {
    int pipeFd[2]{-1, -1};
    const bool hasNext{i + 1 < commands.size()};
    if (hasNext && pipe(pipeFd) != 0)
    {
      perror("pipe");
      if (prevReadFd != -1)
        close(prevReadFd);
      return 1;
    }

    pid_t pid{fork()};
    if (pid == 0)
    {
      if (prevReadFd != -1 && dup2(prevReadFd, STDIN_FILENO) < 0)
      {
        perror("dup2");
        _exit(127);
      }

      if (hasNext && !commands[i].stdoutRedir.enabled)
      {
        if (dup2(pipeFd[1], STDOUT_FILENO) < 0)
        {
          perror("dup2");
          _exit(127);
        }
      }

      if (prevReadFd != -1)
        close(prevReadFd);
      if (hasNext)
      {
        close(pipeFd[0]);
        close(pipeFd[1]);
      }

      auto applyRedirection{[&](const OutputRedirection &redir, int targetFd)
                            {
                              if (!redir.enabled)
                                return true;

                              int fd{open(redir.file.c_str(),
                                          O_WRONLY | O_CREAT | (redir.append ? O_APPEND : O_TRUNC),
                                          0644)};
                              if (fd < 0)
                              {
                                perror("open");
                                return false;
                              }
                              if (dup2(fd, targetFd) < 0)
                              {
                                perror("dup2");
                                close(fd);
                                return false;
                              }
                              close(fd);
                              return true;
                            }};

      if (!applyRedirection(commands[i].stdoutRedir, STDOUT_FILENO))
        _exit(127);
      if (!applyRedirection(commands[i].stderrRedir, STDERR_FILENO))
        _exit(127);

      const auto cmd{this->commands.find(commands[i].args[0])};
      if (cmd != this->commands.end())
      {
        int rc{cmd->second(commands[i].args)};
        _exit(rc);
      }

      auto path{findExecutable(commands[i].args[0])};
      if (path)
      {
        std::vector<char *> argv{argvHelper(commands[i].args)};
        extern char **environ;
        execve(path->c_str(), argv.data(), environ);
        perror("execve");
        _exit(127);
      }

      std::cerr << commands[i].args[0] << ": command not found\n";
      _exit(127);
    }
    else if (pid > 0)
    {
      pids.push_back(pid);
      if (prevReadFd != -1)
        close(prevReadFd);
      if (hasNext)
      {
        close(pipeFd[1]);
        prevReadFd = pipeFd[0];
      }
      else
      {
        prevReadFd = -1;
      }
    }
    else
    {
      perror("fork");
      if (prevReadFd != -1)
        close(prevReadFd);
      if (hasNext)
      {
        close(pipeFd[0]);
        close(pipeFd[1]);
      }
      return 127;
    }
  }

  if (prevReadFd != -1)
    close(prevReadFd);

  int lastStatus{0};
  for (std::size_t i{}; i < pids.size(); ++i)
  {
    int status{};
    waitpid(pids[i], &status, 0);
    if (i + 1 == pids.size())
      lastStatus = status;
  }

  return WIFEXITED(lastStatus) ? WEXITSTATUS(lastStatus) : 127;
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
  return runSingleCommand(command);
}

std::vector<char *> Shell::argvHelper(const std::vector<std::string> &parts)
{
  std::vector<char *> argv{};
  argv.reserve(parts.size() + 1);
  for (auto &s : parts)
    argv.push_back(const_cast<char *>(s.c_str()));
  argv.push_back(nullptr);

  return argv;
}

int Shell::externalCommand(const std::string &path,
                           const std::vector<std::string> &parts,
                           const OutputRedirection &stdoutRedir,
                           const OutputRedirection &stderrRedir)
{

  pid_t pid{fork()};
  if (pid == 0)
  {
    std::vector<char *> argv{argvHelper(parts)};

    if (stdoutRedir.enabled)
    {
      int fd{open(stdoutRedir.file.c_str(),
                  O_WRONLY | O_CREAT | (stdoutRedir.append ? O_APPEND : O_TRUNC),
                  0644)};
      if (fd < 0)
      {
        perror("open");
        _exit(127);
      }
      if (dup2(fd, STDOUT_FILENO) < 0)
      {
        perror("dup2");
        close(fd);
        _exit(127);
      }
      close(fd);
    }

    if (stderrRedir.enabled)
    {
      int fd{open(stderrRedir.file.c_str(),
                  O_WRONLY | O_CREAT | (stderrRedir.append ? O_APPEND : O_TRUNC),
                  0644)};
      if (fd < 0)
      {
        perror("open");
        _exit(127);
      }
      if (dup2(fd, STDERR_FILENO) < 0)
      {
        perror("dup2");
        close(fd);
        _exit(127);
      }
      close(fd);
    }

    extern char **environ;
    execve(path.c_str(), argv.data(), environ);
    perror("execve");
    _exit(127);
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

int Shell::runType(const std::vector<std::string> &args) const
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

  if (chdir(target.c_str()) != 0)
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

std::optional<std::string> Shell::findExecutable(const std::string &name) const
{
  const char *pathEnv{std::getenv("PATH")};
  if (!pathEnv || name.empty())
    return std::nullopt;

  const std::string pathValue{pathEnv};
  std::string segment{};

  auto checkSegment{[&](const std::string &dir) -> std::optional<std::string>
                    {
                      if (dir.empty())
                        return std::nullopt;
                      const std::filesystem::path candidate{std::filesystem::path(dir) / name};
                      if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate) && isExecutable(candidate))
                        return candidate.string();
                      return std::nullopt;
                    }};

  for (char c : pathValue)
  {
    if (c == ':' || c == ';')
    {
      if (auto found{checkSegment(segment)}; found)
        return found;
      segment.clear();
      continue;
    }
    segment.push_back(c);
  }

  if (auto found{checkSegment(segment)}; found)
    return found;

  return std::nullopt;
}

bool Shell::isExecutable(const std::filesystem::path &path) const
{
  std::error_code ec{};
  const auto perms{std::filesystem::status(path, ec).permissions()};
  if (ec)
    return false;

  using std::filesystem::perms;
  const auto mask{perms::owner_exec | perms::group_exec | perms::others_exec};
  return (perms::none != (perms & mask));
}

std::optional<std::string> Shell::getEnvValue(const std::string &key) const
{
  const std::string prefix{key + "="};
  for (const auto &entry : envp)
  {
    if (entry.compare(0, prefix.size(), prefix) == 0)
      return entry.substr(prefix.size());
  }
  return std::nullopt;
}

void Shell::setEnvValue(const std::string &key, const std::string &value)
{
  const std::string prefix{key + "="};
  for (auto &entry : envp)
  {
    if (entry.compare(0, prefix.size(), prefix) == 0)
    {
      entry = prefix + value;
      setenv(key.c_str(), value.c_str(), 1);
      return;
    }
  }

  envp.push_back(prefix + value);
  setenv(key.c_str(), value.c_str(), 1);
}

std::optional<std::string> Shell::getCurrentDir() const
{
  char *cwd{::getcwd(nullptr, 0)};
  if (!cwd)
    return std::nullopt;

  std::string result{cwd};
  std::free(cwd);
  return result;
}

void Shell::run()
{
  activeShell = this;
  rl_initialize();
  rl_bind_key('\t', &Shell::handleTab);

  std::string buffer{};
  bool awaitingContinuation{false};

  while (true)
  {
    const char *prompt{awaitingContinuation ? "> " : "$ "};
    char *input{readline(prompt)};
    if (!input)
    {
      if (awaitingContinuation)
      {
        std::cerr << "syntax error: unexpected end of file\n";
        buffer.clear();
        awaitingContinuation = false;
        continue;
      }
      break;
    }

    std::string line{input};
    std::free(input);

    if (!buffer.empty())
      buffer.push_back('\n');
    buffer += line;

    const auto parts{tokenize(buffer)};
    if (!parts.empty() && parts.back() == "|")
    {
      awaitingContinuation = true;
      continue;
    }

    awaitingContinuation = false;
    if (!buffer.empty())
      add_history(buffer.c_str());
    runCommand(parts);
    buffer.clear();
  }
}
