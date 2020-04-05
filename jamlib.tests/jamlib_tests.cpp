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
      std::string result = JAM::convert_wstring_to_string(out.str());
      out.clear();
      out.str(L"");
      return result;
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

  struct test_command_a : text_fixture
    {
    void test()
      {
      auto result = handle_command(state, "a/Test/,p");
      TEST_ASSERT(result != std::nullopt);
      TEST_EQ("The quick brown fox jumps over the lazy dogTest\n", get_output());
      }
    };

  struct test_command_c : text_fixture
    {
    void test()
      {
      auto result = handle_command(state, ", c/AAA/ ,p");
      TEST_ASSERT(result != std::nullopt);
      TEST_EQ("AAA\n", get_output());
      }
    };

  struct test_command_x : text_fixture
    {
    void test()
      {
      auto result = handle_command(state, ", c/AAA/");
      TEST_ASSERT(result != std::nullopt);
      result = handle_command(*result, "x/B*/ c/-/ ,p");
      TEST_ASSERT(result != std::nullopt);
      TEST_EQ("-A-A-A\n", get_output());
      }
    };

  struct test_command_addresses : text_fixture
    {
    void test()
      {
      auto result = handle_command(state, "a/\\nSecond line/ 2p");
      TEST_ASSERT(result != std::nullopt);
      TEST_EQ("Second line\n", get_output());
      result = handle_command(*result, "$-#4,$ p");
      TEST_EQ("line\n", get_output());
      result = handle_command(*result, "a/\\nThird line/ $-1,$ p");
      TEST_EQ("Second line\nThird line\n", get_output());
      result = handle_command(state, ",p");
      TEST_EQ("The quick brown fox jumps over the lazy dog\n", get_output());
      }
    };

  }

void run_all_jamlib_tests()
  {
  test_command_p().test();
  test_command_q().test();
  test_command_a().test();
  test_command_c().test();
  test_command_x().test();
  test_command_addresses().test();
  }