#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

class Trie
{
public:
  Trie() = default;

  enum class NodeKind
  {
    NotExecutable,
    Builtin,
    PathExecutable
  };

  void clear();
  void insert(std::string_view word);
  void insert(std::string_view word, NodeKind nodeKind);
  bool contains(std::string_view word) const;
  bool hasPrefix(std::string_view prefix) const;
  std::size_t countWithPrefix(std::string_view prefix) const;
  std::optional<std::string> uniqueCompletion(std::string_view prefix) const;
  std::string longestCommonPrefix(std::string_view prefix) const;

private:
  struct Node
  {
    std::unordered_map<char, std::unique_ptr<Node>> children{};
    NodeKind nodeKind{NodeKind::NotExecutable};
    std::size_t subtreeCount{0};
  };

  Node root{};

  Node *findNode(std::string_view text);
  const Node *findNode(std::string_view text) const;
};
