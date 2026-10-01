// Standalone test for platform/common/SingleInstance.h:
//   c++ -std=c++17 -I src tests/platform/single_instance_test.cpp -o /tmp/si_test && /tmp/si_test
// Run as `si_test hold <dir>` in a second process to prove the lock is per process.
#include "platform/common/SingleInstance.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(int argc, char** argv)
{
   if (argc == 3 && std::strcmp(argv[1], "try") == 0)
      return SingleInstance::BecomePrimary(argv[2]) ? 10 : 11; // 11 = someone else is primary

   const std::string dir = (std::filesystem::temp_directory_path() / "infinite_si_test").string();
   std::filesystem::remove_all(dir);

   CHECK(SingleInstance::BecomePrimary(dir));

   // A second process must see the lock held.
   const std::string cmd = std::string(argv[0]) + " try '" + dir + "'";
   const int rc = std::system(cmd.c_str());
#if !defined(_WIN32)
   CHECK(WEXITSTATUS(rc) == 11);
#else
   CHECK(rc == 11);
#endif

   std::string out;
   CHECK(!SingleInstance::Poll(dir, out)); // empty spool
   CHECK(SingleInstance::Forward(dir, "a.inf"));
   CHECK(SingleInstance::Forward(dir, "b with space.inf"));
   CHECK(SingleInstance::Poll(dir, out));
   CHECK(std::filesystem::path(out).is_absolute());
   CHECK(out.size() >= 5 && out.substr(out.size() - 5) == "a.inf"); // oldest first
   CHECK(SingleInstance::Poll(dir, out));
   CHECK(out.find("b with space.inf") != std::string::npos);
   CHECK(!SingleInstance::Poll(dir, out)); // drained

   std::filesystem::remove_all(dir);
   std::printf(fails == 0 ? "single_instance: ok\n" : "single_instance: %d failure(s)\n", fails);
   return fails == 0 ? 0 : 1;
}
