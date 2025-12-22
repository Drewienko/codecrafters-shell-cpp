#include "shell.hpp"

#include <iostream>

int main(int argc, char *argv[], char **envp)
{
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  Shell shell{argc, argv, envp};
  ::write(STDOUT_FILENO, "\x07", 1);
  shell.run();
  return 0;
}
