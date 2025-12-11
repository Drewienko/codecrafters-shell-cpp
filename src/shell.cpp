#include "shell.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <system_error>
#include <utility>

Shell::Shell()
{
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

  std::cerr << parts[0] << ": command not found" << std::endl;
  return 127;
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
