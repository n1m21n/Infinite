// Standalone checks for the Sketch engine (no GL, no app): exit criteria from
// docs/plans/sketch/README.md that don't need a window. Prints one line per check;
// exits non-zero if any fails. With an argument, writes the plan example to that PPM.
#include "SketchEngine.h"
#include "SketchPresets.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int gFail = 0;
static void Check(bool ok, const char* what)
{
   std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
   if (!ok) ++gFail;
}
static size_t NonZeroAlpha(const std::vector<uint8_t>& v)
{
   size_t n = 0;
   for (size_t i = 3; i < v.size(); i += 4) n += v[i] != 0;
   return n;
}
static uint64_t Hash(const std::vector<uint8_t>& v)
{
   uint64_t h = 1469598103934665603ull;
   for (uint8_t b : v) { h ^= b; h *= 1099511628211ull; }
   return h;
}

static const char* kPlanExample = R"(
param("count", 12, 1, 64);
param("spin", 0.2, 0, 2);
function draw(t) {
  background(0.05);
  translate(width / 2, height / 2);
  for (let i = 0; i < count; i++) {
    rotate(TAU / count + spin * t);
    fill(hsl(i / count, 0.7, 0.6));
    circle(200 + 80 * sin(t + i), 0, 30);
  }
}
)";

int main(int argc, char** argv)
{
   SketchEngine e;
   SketchError err;
   std::vector<uint8_t> px;
   SketchEngine::Frame f;
   f.width = 1920; f.height = 1080;

   Check(e.Compile(kPlanExample, err), "plan example compiles");
   Check(e.Params().size() == 2 && e.Params()[0].name == "count", "param() declares knobs");
   Check(e.Run(f, px, err), "plan example runs");
   Check(px.size() == 1920u * 1080 * 4, "output is width*height*4");
   Check(px[3] == 255 && NonZeroAlpha(px) == 1920u * 1080, "background covers frame, opaque");

   // Param drives the picture.
   std::map<std::string, float> pm{{"count", 3}};
   f.params = &pm;
   std::vector<uint8_t> a, b;
   e.Run(f, a, err);
   pm["count"] = 40;
   e.Run(f, b, err);
   Check(Hash(a) != Hash(b), "changing a param changes the image");

   // Reproducible.
   f.params = nullptr; f.t = 1.5;
   std::vector<uint8_t> r1, r2;
   e.Run(f, r1, err); e.Run(f, r2, err);
   Check(Hash(r1) == Hash(r2), "same inputs give identical pixels");

   // 1080p cook time (CPU raster).
   auto t0 = std::chrono::steady_clock::now();
   for (int i = 0; i < 10; ++i) e.Run(f, r1, err);
   double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
   std::printf("INFO  plan example 1080p: %.2f ms/frame\n", ms);

   // Seeded random is reproducible per frame and differs across frames.
   SketchEngine e2;
   e2.Compile("function draw(t){ background(0); fill(1); for(let i=0;i<50;i++) circle(random(width),random(height),8); }", err);
   SketchEngine::Frame g; g.width = 256; g.height = 256; g.frame = 1; g.seed = 7;
   std::vector<uint8_t> x1, x2, x3;
   e2.Run(g, x1, err); e2.Run(g, x2, err); g.frame = 2; e2.Run(g, x3, err);
   Check(Hash(x1) == Hash(x2) && Hash(x1) != Hash(x3), "random() reproducible per (seed, frame)");

   // Runaway loop is aborted; engine survives.
   SketchEngine e3;
   Check(e3.Compile("function draw(t){ while(true){} }", err), "runaway sketch compiles");
   t0 = std::chrono::steady_clock::now();
   bool ran = e3.Run(g, x1, err);
   ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
   Check(!ran && err.Any() && ms < 500, "while(true){} is aborted with an error");
   std::printf("INFO  abort took %.0f ms: %s\n", ms, err.message.c_str());
   Check(e3.Compile("function draw(t){ background(0.5); }", err) && e3.Run(g, x1, err), "engine usable after abort");

   // Memory bomb.
   SketchEngine e4;
   e4.Compile("function draw(t){ const a=[]; for(;;) a.push(new Array(1e6).fill(1)); }", err);
   Check(!e4.Run(g, x1, err) && err.Any(), "memory bomb is stopped");

   // Errors carry line numbers and keep the last working program.
   SketchEngine e5;
   e5.Compile("function draw(t){ background(0.25); }", err);
   Check(!e5.Compile("function draw(t){\n  background(0.25);\n  nope();\n}\nfoo bar", err) && err.line == 5, "syntax error reports line");
   Check(e5.HasProgram() && e5.Run(g, x1, err), "failed compile keeps last working program");
   e5.Compile("function draw(t){\n  background(0.25);\n  nope();\n}", err);
   Check(!e5.Run(g, x1, err) && err.line == 3, "runtime error reports line");

   // No file/network/eval escape hatches from quickjs-libc.
   SketchEngine e6;
   e6.Compile("function draw(t){ if (typeof std!=='undefined'||typeof os!=='undefined'||typeof require!=='undefined') throw new Error('leak'); background(0); }", err);
   Check(e6.Run(g, x1, err), "no std/os/require in the sandbox");

   // Every shipped preset compiles, runs, draws something, and prints a pixel hash
   // (compare these across OSes for the identical-pixels exit criterion).
   {
      const char* font = std::getenv("SKETCH_FONT");
      const char* dump = std::getenv("SKETCH_DUMP_DIR");
      int n = 0;
      const SketchPresets::Entry* pe = SketchPresets::All(n);
      for (int i = 0; i < n; ++i)
      {
         SketchEngine pr;
         if (font) pr.SetFontFile(font);
         SketchEngine::Frame pf; pf.width = 1920; pf.height = 1080; pf.t = 2.0; pf.frame = 120; pf.seed = 1;
         std::vector<uint8_t> o;
         Check(pr.Compile(pe[i].code, err), (std::string("preset compiles: ") + pe[i].name).c_str());
         bool ok = pr.Run(pf, o, err);
         Check(ok, (std::string("preset runs: ") + pe[i].name + " " + err.message).c_str());
         if (!ok) continue;
         auto t1 = std::chrono::steady_clock::now();
         for (int k = 0; k < 5; ++k) pr.Run(pf, o, err);
         double pms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / 5;
         std::printf("INFO  preset %-14s %6.2f ms  hash %016llx\n", pe[i].name, pms, (unsigned long long)Hash(o));
         if (dump)
         {
            std::string path = std::string(dump) + "/" + std::to_string(i) + ".ppm";
            if (FILE* fp = std::fopen(path.c_str(), "wb"))
            {
               std::fprintf(fp, "P6\n%d %d\n255\n", pf.width, pf.height);
               for (size_t q = 0; q < o.size(); q += 4) std::fwrite(&o[q], 1, 3, fp);
               std::fclose(fp);
            }
         }
      }
   }

   if (argc > 1)
   {
      f.t = 2.0;
      e.Run(f, px, err);
      if (FILE* fp = std::fopen(argv[1], "wb"))
      {
         std::fprintf(fp, "P6\n%d %d\n255\n", f.width, f.height);
         for (size_t i = 0; i < px.size(); i += 4) std::fwrite(&px[i], 1, 3, fp);
         std::fclose(fp);
      }
   }
   std::printf(gFail ? "%d FAILED\n" : "all passed\n", gFail);
   return gFail ? 1 : 0;
}
