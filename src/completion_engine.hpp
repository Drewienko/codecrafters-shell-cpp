#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "completion_state.hpp"
#include "path_resolver.hpp"
#include "trie.hpp"

class CompletionEngine
{
public:
  CompletionEngine() = default;

  void registerBuiltin(const std::string &name);
  void refreshExecutables();

  class ActiveGuard
  {
  public:
    explicit ActiveGuard(CompletionEngine &engine);
    ~ActiveGuard();

    ActiveGuard(const ActiveGuard &) = delete;
    ActiveGuard &operator=(const ActiveGuard &) = delete;

  private:
    CompletionEngine *previous{nullptr};
  };

  static int handleTab(int count, int key);

private:
  Trie completionTrie{};
  CompletionState completionState{};
  PathResolver pathResolver{};
  std::vector<std::string> builtinNames{};
  static constexpr std::size_t completionQueryItems{100};

  static CompletionEngine *activeEngine;

  void rebuildTrie();
  void resetState();
  int handleTabImpl();
};
