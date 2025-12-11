#include "shell.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
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

    std::cout << name << ": not found" << std::endl;
  }

  return 0;
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
