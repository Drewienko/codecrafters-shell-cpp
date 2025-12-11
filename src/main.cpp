#include <iostream>
#include <string>

int main()
{
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  std::string buff;
  while (1)
  {
    std::cout << "$ ";
    if (!std::getline(std::cin, buff))
      break;

    const auto first = buff.find_first_not_of(' ');
    if (first == std::string::npos)
      continue;

    const auto space = buff.find(' ', first);
    const std::string cmd = buff.substr(first, space - first);

    if (cmd == "exit")
      break;

    if (cmd == "echo")
    {
      if (space == std::string::npos)
        std::cout << std::endl;
      else
        std::cout << buff.substr(space + 1) << std::endl;
      continue;
    }

    std::cerr << cmd << ": command not found" << std::endl;
  }
}
