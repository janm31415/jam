#include <iostream>
#include <string>
#include <windows.h>
#include <vector>
#include <chrono>
#include <thread>
#include <sstream>

#include <pipe.h>

int main(int, char**)
  {  
  auto input = JAM::read_std_input(50);


  while (!input.empty())
    {
    auto pos = input.find_first_of('\n');
    std::string line = pos == std::string::npos ? input : input.substr(0, pos + 1);
    
    const auto pos2 = line.find_first_not_of(' ');
    if (pos2 == std::string::npos)
      {
      if (line.size() > 4)
        std::cout << line.substr(4);
      else
        std::cout << "\n";
      }
    else
      std::cout << line.substr(pos2 > 4 ? 4 : pos2);

    input.erase(0, pos == std::string::npos ? pos : pos + 1);
    }

  return 0;
  }