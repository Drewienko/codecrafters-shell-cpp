#pragma once

#include <filesystem>
#include <string>

inline std::filesystem::path normalizePath(const std::string &path)
{
  return std::filesystem::path{path}.lexically_normal();
}
