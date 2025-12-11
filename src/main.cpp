#include "shell.hpp"

#include <iostream>

int main()
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  Shell shell;
  shell.run();
  return 0;
}
