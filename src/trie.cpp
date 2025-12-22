#include "trie.hpp"

#include <algorithm>

namespace
{
constexpr auto notExecutable = Trie::NodeKind::NotExecutable;
}

void Trie::clear()
{
  root = Node{};
}

void Trie::insert(std::string_view word)
{
  insert(word, NodeKind::PathExecutable);
}

void Trie::insert(std::string_view word, NodeKind nodeKind)
{
  if (word.empty())
    return;
  if (nodeKind == notExecutable)
    return;

  bool isNewWord{true};
  if (const Node *existing{findNode(word)}; existing && existing->nodeKind != notExecutable)
    isNewWord = false;

  Node *node{&root};
  if (isNewWord)
    node->subtreeCount++;
  for (char c : word)
  {
    auto &child{node->children[c]};
    if (!child)
      child = std::make_unique<Node>();
    node = child.get();
    if (isNewWord)
      node->subtreeCount++;
  }
  if (node->nodeKind == NodeKind::Builtin && nodeKind == NodeKind::PathExecutable)
    return;
  node->nodeKind = nodeKind;
}

bool Trie::contains(std::string_view word) const
{
  const Node *node{findNode(word)};
  return node && node->nodeKind != notExecutable;
}

bool Trie::hasPrefix(std::string_view prefix) const
{
  return findNode(prefix) != nullptr;
}

std::size_t Trie::countWithPrefix(std::string_view prefix) const
{
  const Node *node{findNode(prefix)};
  return node ? node->subtreeCount : 0;
}

std::optional<std::string> Trie::uniqueCompletion(std::string_view prefix) const
{
  const Node *node{findNode(prefix)};
  if (!node || node->subtreeCount != 1)
    return std::nullopt;

  std::string result{prefix};
  while (node && node->nodeKind == notExecutable)
  {
    if (node->children.empty())
      break;
    const auto it{node->children.begin()};
    result.push_back(it->first);
    node = it->second.get();
  }
  return result;
}

std::string Trie::longestCommonPrefix(std::string_view prefix) const
{
  const Node *node{findNode(prefix)};
  if (!node)
    return "";

  std::string result{prefix};
  while (node->children.size() == 1 && node->nodeKind == notExecutable)
  {
    const auto it{node->children.begin()};
    result.push_back(it->first);
    node = it->second.get();
  }
  return result;
}

std::vector<std::string> Trie::collectWithPrefix(std::string_view prefix) const
{
  std::vector<std::string> results{};
  const Node *node{findNode(prefix)};
  if (!node)
    return results;

  std::string current{prefix};
  collectFrom(node, current, results);
  std::sort(results.begin(), results.end());
  return results;
}

Trie::Node *Trie::findNode(std::string_view text)
{
  Node *node{&root};
  for (char c : text)
  {
    auto it{node->children.find(c)};
    if (it == node->children.end())
      return nullptr;
    node = it->second.get();
  }
  return node;
}

const Trie::Node *Trie::findNode(std::string_view text) const
{
  const Node *node{&root};
  for (char c : text)
  {
    auto it{node->children.find(c)};
    if (it == node->children.end())
      return nullptr;
    node = it->second.get();
  }
  return node;
}

void Trie::collectFrom(const Node *node, std::string &current, std::vector<std::string> &results) const
{
  if (!node)
    return;
  if (node->nodeKind != notExecutable)
    results.push_back(current);

  for (const auto &entry : node->children)
  {
    current.push_back(entry.first);
    collectFrom(entry.second.get(), current, results);
    current.pop_back();
  }
}
