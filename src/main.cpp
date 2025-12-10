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
    std::cin >> buff;
    std::cerr << buff << ": command not found" << std::endl;
  }
}
