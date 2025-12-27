#pragma once

#include <functional>
#include <vector>

#include "command.hpp"

class PipelineExecutor
{
public:
  using Runner = std::function<int(const ParsedCommand &, ExecMode)>;

  int run(const std::vector<ParsedCommand> &commands, const Runner &runner) const;
};
