#include "trie.hpp"

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
  if (nodeKind == NodeKind::NotExecutable)
    return;

  bool isNewWord{true};
  if (const Node *existing{findNode(word)}; existing && existing->nodeKind != NodeKind::NotExecutable)
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
  return node && node->nodeKind != NodeKind::NotExecutable;
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
  while (node && node->nodeKind == NodeKind::NotExecutable)
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
  while (node->children.size() == 1 && node->nodeKind == NodeKind::NotExecutable)
  {
    const auto it{node->children.begin()};
    result.push_back(it->first);
    node = it->second.get();
  }
  return result;
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
