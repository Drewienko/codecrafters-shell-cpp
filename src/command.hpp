#pragma once

#include <string>
#include <vector>

struct OutputRedirection
{
  bool enabled{false};
  bool append{false};
  std::string file{};
};

struct ParsedCommand
{
  std::vector<std::string> args{};
  OutputRedirection stdoutRedir{};
  OutputRedirection stderrRedir{};
};

enum class ExecMode
{
  Parent,
  Child
};
