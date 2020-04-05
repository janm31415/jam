#include "jamlib_tests.h"
#include "test_assert.h"

#include <jamlib/jam.h>

#include <utils/encoding.h>

#include <string>
#include <sstream>

using namespace jamlib;

namespace
  {
  struct text_fixture
    {
    text_fixture()
      {
      char* files[2];
      files[0] = nullptr;
#ifdef _WIN32
      files[1] = "data\\text.txt";
#else
      files[1] = "./data/text.txt";
#endif
      state = init_state(2, files);
      set_output_stream(&out);
      }

    ~text_fixture()
      {

      }

    std::string get_output()
      {
      return JAM::convert_wstring_to_string(out.str());
      }

    app_state state;
    std::wstringstream out;
    };

  struct test_command_p : text_fixture
    {
    void test()
      {
      auto result = handle_command(state, ",p");
      TEST_ASSERT(result != std::nullopt);
      TEST_EQ("The quick brown fox jumps over the lazy dog\n", get_output());
      }
    };

  struct test_command_q : text_fixture
    {
    void test()
      {
      auto result = handle_command(state, "q");
      TEST_ASSERT(result == std::nullopt);
      }
    };

  }

void run_all_jamlib_tests()
  {
  test_command_p().test();
  test_command_q().test();
  }