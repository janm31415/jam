#include <jamlib/jam.h>
#include <iostream>

std::string read_command()
  {
  std::string command;
  std::getline(std::cin, command);
  return command;
  }

int main(int argc, char** argv)
  {
  using namespace jamlib;

  app_state state = init_state(argc, argv);
  std::optional<app_state> new_state = state;
  do
    {
    state = *new_state;
    try
      {
      new_state = handle_command(state, read_command());
      }
    catch (std::runtime_error e)
      {
      std::cout << "error: " << e.what() << std::endl;
      new_state = state;
      }    
    } while (new_state);
  return 0;
  }