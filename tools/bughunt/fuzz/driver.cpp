// A small coverage-guided fuzz loop for platforms without libFuzzer (Apple
// clang ships no libclang_rt.fuzzer). It drives the same
// LLVMFuzzerTestOneInput the harnesses export for libFuzzer, so one harness
// file works in both: on Linux build.sh links real libFuzzer, on macOS it links
// this.
//
// Guidance comes from -fsanitize-coverage=trace-pc-guard: an input that hits an
// edge no earlier input hit joins the corpus. Mutations are byte-level (flip,
// interesting values, 2/4-byte integers, chunk insert/erase/copy, splice).
// A sanitizer abort writes the input to <artifact_prefix>crash-<hash> first.
//
//   fuzz_x [corpus_dir_or_file ...] [-runs=N] [-max_len=N] [-seed=N]
//          [-max_total_time=S] [-artifact_prefix=DIR/]
//
// With only files and no -runs it replays them once (repro mode), like libFuzzer.
#include <sanitizer/common_interface_defs.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
extern "C" __attribute__((weak)) int LLVMFuzzerInitialize(int* argc, char*** argv);

namespace fs = std::filesystem;

static std::vector<uint8_t> gHit;   // per guard: hit by any corpus input
static size_t gNewEdges = 0;        // edges the current input added
static const std::vector<uint8_t>* gCurrent = nullptr;
static std::string gArtifactPrefix = "./";

extern "C" void __sanitizer_cov_trace_pc_guard_init(uint32_t* start, uint32_t* stop)
{
   if (start == stop || *start)
      return;
   static uint32_t n = 0;
   for (uint32_t* x = start; x < stop; x++)
      *x = ++n;
   gHit.resize(n + 1, 0);
}

extern "C" void __sanitizer_cov_trace_pc_guard(uint32_t* guard)
{
   const uint32_t g = *guard;
   if (g == 0 || g >= gHit.size())
      return;
   if (!gHit[g])
   {
      gHit[g] = 1;
      gNewEdges++;
   }
   *guard = 0; // each edge reports once per process; gHit keeps the record
}

static uint64_t Hash(const std::vector<uint8_t>& v)
{
   uint64_t h = 1469598103934665603ull;
   for (uint8_t b : v)
      h = (h ^ b) * 1099511628211ull;
   return h;
}

static void WriteFile(const std::string& path, const std::vector<uint8_t>& v)
{
   std::ofstream f(path, std::ios::binary);
   f.write((const char*)v.data(), (std::streamsize)v.size());
}

static void OnDeath()
{
   if (gCurrent == nullptr)
      return;
   char name[64];
   snprintf(name, sizeof(name), "crash-%016llx", (unsigned long long)Hash(*gCurrent));
   const std::string path = gArtifactPrefix + name;
   WriteFile(path, *gCurrent);
   fprintf(stderr, "==driver== input that crashed written to %s (%zu bytes)\n", path.c_str(), gCurrent->size());
}

static size_t Run(const std::vector<uint8_t>& in)
{
   gNewEdges = 0;
   gCurrent = &in;
   LLVMFuzzerTestOneInput(in.empty() ? nullptr : in.data(), in.size());
   gCurrent = nullptr;
   return gNewEdges;
}

static void Mutate(std::vector<uint8_t>& v, const std::vector<std::vector<uint8_t>>& corpus, std::mt19937_64& r,
                   size_t maxLen)
{
   static const uint32_t kInteresting[] = { 0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000u,
                                            0xFFFFFFFFu, 16, 32, 64, 100, 1000, 4096, 65536 };
   const int n = 1 + (int)(r() % 4);
   for (int k = 0; k < n; k++)
   {
      const size_t sz = v.size();
      switch (r() % 8)
      {
      case 0: // flip a bit
         if (sz) v[r() % sz] ^= (uint8_t)(1u << (r() % 8));
         break;
      case 1: // interesting byte
         if (sz) v[r() % sz] = (uint8_t)kInteresting[r() % 5];
         break;
      case 2: // interesting 16/32-bit integer, either endianness
         if (sz >= 4)
         {
            const uint32_t x = kInteresting[r() % (sizeof(kInteresting) / 4)];
            const size_t at = r() % (sz - 3);
            const bool be = r() & 1;
            const int w = (r() & 1) ? 4 : 2;
            for (int i = 0; i < w; i++)
               v[at + i] = (uint8_t)(x >> (8 * (be ? (w - 1 - i) : i)));
         }
         break;
      case 3: // random byte
         if (sz) v[r() % sz] = (uint8_t)r();
         break;
      case 4: // erase a chunk
         if (sz > 1)
         {
            const size_t at = r() % sz, len = 1 + r() % std::min<size_t>(sz - at, 64);
            v.erase(v.begin() + (long)at, v.begin() + (long)(at + len));
         }
         break;
      case 5: // insert random bytes
         if (sz < maxLen)
         {
            const size_t at = sz ? r() % (sz + 1) : 0, len = 1 + r() % 32;
            std::vector<uint8_t> ins(len);
            for (auto& b : ins) b = (uint8_t)r();
            v.insert(v.begin() + (long)at, ins.begin(), ins.end());
         }
         break;
      case 6: // duplicate a chunk elsewhere
         if (sz > 2 && sz < maxLen)
         {
            const size_t from = r() % sz, len = 1 + r() % std::min<size_t>(sz - from, 128);
            std::vector<uint8_t> c(v.begin() + (long)from, v.begin() + (long)(from + len));
            v.insert(v.begin() + (long)(r() % (sz + 1)), c.begin(), c.end());
         }
         break;
      case 7: // splice the tail of another corpus input
         if (!corpus.empty())
         {
            const auto& o = corpus[r() % corpus.size()];
            if (!o.empty())
            {
               const size_t cut = sz ? r() % sz : 0, from = r() % o.size();
               v.resize(cut);
               v.insert(v.end(), o.begin() + (long)from, o.end());
            }
         }
         break;
      }
   }
   if (v.size() > maxLen)
      v.resize(maxLen);
}

int main(int argc, char** argv)
{
   if (LLVMFuzzerInitialize)
      LLVMFuzzerInitialize(&argc, &argv);
   long long runs = -1;
   size_t maxLen = 1 << 16;
   uint64_t seed = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
   double maxTime = 0;
   std::vector<std::string> inputs;
   for (int i = 1; i < argc; i++)
   {
      const std::string a = argv[i];
      if (a.rfind("-runs=", 0) == 0) runs = atoll(a.c_str() + 6);
      else if (a.rfind("-max_len=", 0) == 0) maxLen = (size_t)atoll(a.c_str() + 9);
      else if (a.rfind("-seed=", 0) == 0) seed = (uint64_t)atoll(a.c_str() + 6);
      else if (a.rfind("-max_total_time=", 0) == 0) maxTime = atof(a.c_str() + 16);
      else if (a.rfind("-artifact_prefix=", 0) == 0) gArtifactPrefix = a.substr(17);
      else if (a[0] == '-') fprintf(stderr, "==driver== ignoring %s\n", a.c_str());
      else inputs.push_back(a);
   }
   __sanitizer_set_death_callback(OnDeath);

   std::vector<std::vector<uint8_t>> corpus;
   std::string corpusDir;
   bool onlyFiles = true;
   auto load = [&](const fs::path& p) {
      std::ifstream f(p, std::ios::binary);
      corpus.emplace_back((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
   };
   for (const std::string& in : inputs)
   {
      std::error_code ec;
      if (fs::is_directory(in, ec))
      {
         onlyFiles = false;
         if (corpusDir.empty())
            corpusDir = in;
         for (const auto& e : fs::directory_iterator(in))
            if (e.is_regular_file())
               load(e.path());
      }
      else if (fs::exists(in, ec))
         load(in);
   }
   if (onlyFiles && !inputs.empty() && runs < 0)
   {
      for (size_t i = 0; i < corpus.size(); i++)
      {
         fprintf(stderr, "==driver== running %s (%zu bytes)\n", inputs[i].c_str(), corpus[i].size());
         Run(corpus[i]);
      }
      fprintf(stderr, "==driver== replayed %zu inputs, no crash\n", corpus.size());
      return 0;
   }
   if (corpus.empty())
      corpus.push_back({});
   size_t edges = 0;
   for (const auto& c : corpus)
      edges += Run(c);
   fprintf(stderr, "==driver== seed corpus %zu inputs, %zu edges\n", corpus.size(), edges);

   std::mt19937_64 r(seed);
   const auto t0 = std::chrono::steady_clock::now();
   long long done = 0;
   for (; runs < 0 || done < runs; done++)
   {
      if (maxTime > 0 && (done & 255) == 0 &&
          std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > maxTime)
         break;
      std::vector<uint8_t> v = corpus[r() % corpus.size()];
      Mutate(v, corpus, r, maxLen);
      const size_t n = Run(v);
      if (n)
      {
         edges += n;
         corpus.push_back(v);
         if (!corpusDir.empty())
         {
            char name[32];
            snprintf(name, sizeof(name), "%016llx", (unsigned long long)Hash(v));
            WriteFile(corpusDir + "/" + name, v);
         }
      }
      if ((done & 0xFFF) == 0)
         fprintf(stderr, "==driver== #%lld corpus %zu edges %zu\n", done, corpus.size(), edges);
   }
   fprintf(stderr, "==driver== done: %lld runs, corpus %zu, edges %zu\n", done, corpus.size(), edges);
   return 0;
}
