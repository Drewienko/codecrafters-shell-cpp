#include "shell.hpp"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <system_error>
#include <utility>

Shell::Shell(int argc, char *argv[], char **envp)
{
  this->argv.reserve(static_cast<std::size_t>(argc));
  for (int i{}; i < argc; ++i)
    this->argv.emplace_back(argv[i] ? argv[i] : "");

  for (char **env = envp; env && *env; ++env)
    this->envp.emplace_back(*env);

  registerBuiltin("exit", [](const auto &)
                  {
    std::exit(0);
    return 0; });

  registerBuiltin("echo", [](const auto &args)
                  {
    for (std::size_t i = 1; i < args.size(); ++i)
    {
      if (i > 1)
        std::cout << ' ';
      std::cout << args[i];
    }
    std::cout << '\n';
    return 0; });

  registerBuiltin("type", [this](const auto &args)
                  { return runType(args); });

  registerBuiltin("pwd", [this](const auto &)
                  { return runPwd(); });

  registerBuiltin("cd", [this](const auto &args)
                  { return runCd(args); });
}

void Shell::registerBuiltin(const std::string &name, CommandHandler handler)
{
  commands[name] = std::move(handler);
}

std::vector<std::string> Shell::tokenize(const std::string &line) const
{
  std::vector<std::string> parts;
  std::string currentToken;
  bool tokenStarted = false;

  enum class Mode
  {
    None,
    Single,
    Double
  };

  Mode mode = Mode::None;

  auto pushToken = [&]()
  {
    if (tokenStarted)
      parts.push_back(currentToken);
    currentToken.clear();
    tokenStarted = false;
  };

  for (std::size_t i{}; i < line.size(); ++i)
  {
    char c = line[i];

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
        char next = line[i + 1];
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

int Shell::runCommand(const std::vector<std::string> &parts)
{
  if (parts.empty())
    return 0;

  OutputRedirection redir;
  std::vector<std::string> args;
  args.reserve(parts.size());
  for (std::size_t i = 0; i < parts.size(); ++i)
  {
    const std::string &token = parts[i];
    bool append = false;
    bool isRedir = false;

    if (token == ">" || token == "1>")
      isRedir = true;
    else if (token == ">>" || token == "1>>")
    {
      isRedir = true;
      append = true;
    }

    if (isRedir)
    {
      if (i + 1 >= parts.size())
      {
        std::cerr << "syntax error: missing file for redirection" << std::endl;
        return 1;
      }
      redir.enabled = true;
      redir.append = append;
      redir.file = parts[i + 1];
      ++i;
      continue;
    }

    args.push_back(token);
  }

  if (args.empty())
    return 0;

  const auto cmd = commands.find(args[0]);
  if (cmd != commands.end())
  {
    if (!redir.enabled)
      return cmd->second(args);

    int saved = dup(STDOUT_FILENO);
    if (saved < 0)
    {
      perror("dup");
      return 1;
    }

    int fd = open(redir.file.c_str(),
                  O_WRONLY | O_CREAT | (redir.append ? O_APPEND : O_TRUNC),
                  0644);
    if (fd < 0)
    {
      perror("open");
      close(saved);
      return 1;
    }

    if (dup2(fd, STDOUT_FILENO) < 0)
    {
      perror("dup2");
      close(fd);
      close(saved);
      return 1;
    }
    close(fd);

    int rc = cmd->second(args);
    if (dup2(saved, STDOUT_FILENO) < 0)
      perror("dup2");
    close(saved);
    return rc;
  }

  if (auto path = findExecutable(args[0]))
  {
    std::string execPath = *path + '/' + args[0];
    return externalCommand(*path, args, redir);
  }

  std::cerr << args[0] << ": command not found" << std::endl;
  return 127;
}

std::vector<char *> Shell::argvHelper(const std::vector<std::string> &parts)
{
  std::vector<char *> argv;
  argv.reserve(parts.size() + 1);
  for (auto &s : parts)
    argv.push_back(const_cast<char *>(s.c_str()));
  argv.push_back(nullptr);

  return argv;
}

int Shell::externalCommand(const std::string &path, const std::vector<std::string> &parts, const OutputRedirection &redir)
{

  pid_t pid = fork();
  if (pid == 0)
  {
    std::vector<char *> argv = argvHelper(parts);

    if (redir.enabled)
    {
      int fd = open(redir.file.c_str(),
                    O_WRONLY | O_CREAT | (redir.append ? O_APPEND : O_TRUNC),
                    0644);
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

    extern char **environ;
    execve(path.c_str(), argv.data(), environ);
    perror("execve");
    _exit(127);
  }
  else if (pid > 0)
  {
    int status = 0;
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

  for (std::size_t i = 1; i < args.size(); ++i)
  {
    const auto &name = args[i];

    if (commands.find(name) != commands.end())
    {
      std::cout << name << " is a shell builtin" << std::endl;
      continue;
    }

    if (auto path = findExecutable(name))
    {
      std::cout << name << " is " << *path << std::endl;
      continue;
    }

    std::cout << name << ": not found" << std::endl;
  }

  return 0;
}

int Shell::runPwd()
{
  if (const char *pwd = std::getenv("PWD"); pwd && *pwd)
  {
    std::cout << pwd << std::endl;
    return 0;
  }

  if (auto pwd = getEnvValue("PWD"))
  {
    std::cout << *pwd << std::endl;
    return 0;
  }

  std::cerr << "pwd: PWD not set" << std::endl;
  return 1;
}

int Shell::runCd(const std::vector<std::string> &args)
{
  std::string target;
  if (args.size() < 2)
  {
    const char *home = std::getenv("HOME");
    if (!home || *home == '\0')
    {
      std::cerr << "cd: HOME not set" << std::endl;
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
      const char *home = std::getenv("HOME");
      if (!home || *home == '\0')
      {
        std::cerr << "cd: HOME not set" << std::endl;
        return 1;
      }
      target = std::string(home) + target.substr(1);
    }
  }

  std::optional<std::string> oldpwd = getCurrentDir();
  if (!oldpwd)
    oldpwd = getEnvValue("PWD");

  if (chdir(target.c_str()) != 0)
  {
    std::cerr << "cd: " << target << ": " << std::strerror(errno) << std::endl;
    return 1;
  }

  std::string newpwd;
  if (auto cwd = getCurrentDir())
    newpwd = *cwd;
  else
    newpwd = target;

  if (oldpwd)
    setEnvValue("OLDPWD", *oldpwd);
  setEnvValue("PWD", newpwd);
  return 0;
}

std::optional<std::string> Shell::findExecutable(const std::string &name) const
{
  const char *pathEnv = std::getenv("PATH");
  if (!pathEnv || name.empty())
    return std::nullopt;

  const std::string pathValue(pathEnv);
  std::string segment;

  auto checkSegment = [&](const std::string &dir) -> std::optional<std::string>
  {
    if (dir.empty())
      return std::nullopt;
    const std::filesystem::path candidate = std::filesystem::path(dir) / name;
    if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate) && isExecutable(candidate))
      return candidate.string();
    return std::nullopt;
  };

  for (char c : pathValue)
  {
    if (c == ':' || c == ';')
    {
      if (auto found = checkSegment(segment))
        return found;
      segment.clear();
      continue;
    }
    segment.push_back(c);
  }

  if (auto found = checkSegment(segment))
    return found;

  return std::nullopt;
}

bool Shell::isExecutable(const std::filesystem::path &path) const
{
  std::error_code ec;
  const auto perms = std::filesystem::status(path, ec).permissions();
  if (ec)
    return false;

  using std::filesystem::perms;
  const auto mask = perms::owner_exec | perms::group_exec | perms::others_exec;
  return (perms::none != (perms & mask));
}

std::optional<std::string> Shell::getEnvValue(const std::string &key) const
{
  const std::string prefix = key + "=";
  for (const auto &entry : envp)
  {
    if (entry.compare(0, prefix.size(), prefix) == 0)
      return entry.substr(prefix.size());
  }
  return std::nullopt;
}

void Shell::setEnvValue(const std::string &key, const std::string &value)
{
  const std::string prefix = key + "=";
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
  char *cwd = ::getcwd(nullptr, 0);
  if (!cwd)
    return std::nullopt;

  std::string result(cwd);
  std::free(cwd);
  return result;
}

void Shell::run()
{
  std::string line;
  while (true)
  {
    std::cout << "$ ";
    if (!std::getline(std::cin, line))
      break;

    const auto parts = tokenize(line);
    runCommand(parts);
  }
}
