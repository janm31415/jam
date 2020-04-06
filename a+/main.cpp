#include <iostream>
#include <jam_pipe.h>

int main(int, char**)
  {  
  auto input = JAM::read_std_input(50);

  while (!input.empty())
    {
    auto pos = input.find_first_of('\n');
    std::string line = pos == std::string::npos ? input : input.substr(0, pos + 1);
    std::cout << "    " << line;
    input.erase(0, pos == std::string::npos ? pos : pos + 1);
    }

  return 0;
  }