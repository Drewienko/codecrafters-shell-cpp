#pragma once

#include <cstddef>
#include <string>

class CompletionState
{
public:
  void reset()
  {
    pendingList = false;
    pendingLine.clear();
    pendingPoint = 0;
  }

  bool isPendingFor(const std::string &line, std::size_t point) const
  {
    return pendingList && pendingLine == line && pendingPoint == point;
  }

  void markPending(const std::string &line, std::size_t point)
  {
    pendingList = true;
    pendingLine = line;
    pendingPoint = point;
  }

private:
  bool pendingList{false};
  std::string pendingLine{};
  std::size_t pendingPoint{0};
};
