#include "completion_engine.hpp"

#include <cctype>
#include <iostream>
#include <readline/readline.h>
#include <unistd.h>

CompletionEngine *CompletionEngine::activeEngine{nullptr};

void CompletionEngine::registerBuiltin(const std::string &name)
{
  builtinNames.push_back(name);
  completionTrie.insert(name, Trie::NodeKind::Builtin);
}

void CompletionEngine::refreshExecutables()
{
  if (!pathResolver.refresh())
    return;
  rebuildTrie();
}

void CompletionEngine::rebuildTrie()
{
  completionTrie.clear();
  for (const auto &name : builtinNames)
    completionTrie.insert(name, Trie::NodeKind::Builtin);

  pathResolver.forEachExecutable([&](const std::filesystem::path &path)
                                 { completionTrie.insert(path.filename().string(), Trie::NodeKind::PathExecutable); });
}

void CompletionEngine::resetState()
{
  completionState.reset();
}

CompletionEngine::ActiveGuard::ActiveGuard(CompletionEngine &engine)
    : previous{activeEngine}
{
  activeEngine = &engine;
}

CompletionEngine::ActiveGuard::~ActiveGuard()
{
  activeEngine = previous;
}

int CompletionEngine::handleTab(int, int)
{
  if (!activeEngine)
    return 0;
  return activeEngine->handleTabImpl();
}

int CompletionEngine::handleTabImpl()
{
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
    resetState();
    return 0;
  }

  std::string prefix{line.substr(0, point)};
  if (prefix.empty())
  {
    resetState();
    return 0;
  }

  refreshExecutables();
  auto matches{completionTrie.collectWithPrefix(prefix)};
  if (matches.empty())
  {
    resetState();
    ::write(STDOUT_FILENO, "\x07", 1);
    return 0;
  }

  if (matches.size() == 1)
  {
    resetState();
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

  std::string lcp{completionTrie.longestCommonPrefix(prefix)};
  if (lcp.size() > prefix.size())
  {
    resetState();
    std::string suffix{lcp.substr(prefix.size())};
    rl_insert_text(suffix.c_str());
    ::write(STDOUT_FILENO, "\x07", 1);
    rl_redisplay();

    ::write(STDOUT_FILENO, "\x07", 1);
    return 0;
  }

  if (completionState.isPendingFor(line, point))
  {
    resetState();
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
  completionState.markPending(line, point);
  return 0;
}
