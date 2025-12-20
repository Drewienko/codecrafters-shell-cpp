#include "shell.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>
#include <sstream>
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
  std::istringstream stream(line);
  std::vector<std::string> parts;
  for (std::string token; stream >> token;)
    parts.push_back(token);
  return parts;
}

int Shell::runCommand(const std::vector<std::string> &parts)
{
  if (parts.empty())
    return 0;

  const auto cmd = commands.find(parts[0]);
  if (cmd != commands.end())
    return cmd->second(parts);

  if (auto path = findExecutable(parts[0]))
  {
    std::string execPath = *path + '/' + parts[0];
    return externalCommand(*path, parts);
  }

  std::cerr << parts[0] << ": command not found" << std::endl;
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

int Shell::externalCommand(const std::string &path, const std::vector<std::string> &parts)
{

  pid_t pid = fork();
  if (pid == 0)
  {
    std::vector<char *> argv = argvHelper(parts);

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
