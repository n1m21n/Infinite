#!/usr/bin/env python3
"""W1 ratchet: no narrow file opens in code that Windows compiles.

Engine paths are UTF-8 std::string. On Windows, std::ifstream/ofstream(std::string)
and fopen() read the path in the ANSI code page, so a user called José or a folder
named テスト silently fails to load or save. Open through AppPaths::FsPath(path)
(streams) or AppPaths::OpenFile(path, mode) (FILE*), see src/platform/AppPaths.h.

Skipped: macOS-only (.mm) and Linux-only (src/platform/linux/) sources, and tests.
ALLOW lists the lines that are already safe: the argument is a std::filesystem::path,
or the call sits in a non-Windows #else branch.

Usage: python3 scripts/check_utf8_paths.py   (exit 1 and a list when it finds one)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SKIP_DIRS = ("src/platform/linux/", "src/platform/mac/", "src/app/selftest/")
SAFE_ARG = re.compile(r"^(AppPaths::)?FsPath\(|u8path\(|Utf8ToWide\(|WPATH\(")
OPEN = re.compile(r"std::(i|o)?fstream(\s+\w+)?\s*\(|(?<![\w_])(std::)?fopen\s*\(|\.open\s*\(")

# (file, stripped source line) pairs that are fine as written.
ALLOW = {
    ("src/app/Startup.cpp", 'const auto touch = [&](const char* name) { std::ofstream(dir / name) << "x"; };'),
    ("src/core/Extensions.cpp", 'std::ofstream(f).put(\'x\');'),
    ("src/core/Extensions.cpp", 'FILE* f = fopen(target.c_str(), "wb");'),
    ("src/core/ShareBundle.cpp", "std::ofstream f(dest, std::ios::binary);"),
    ("src/nodes/LooperNode.cpp", "std::ofstream f(tmp, std::ios::binary | std::ios::trunc);"),
    ("src/platform/common/SingleInstance.h", "std::ofstream f(tmp, std::ios::binary | std::ios::trunc);"),
    ("src/platform/common/SingleInstance.h", "std::ifstream f(req, std::ios::binary);"),
    ("src/platform/common/PathOpen.h", "return std::fopen(pathUtf8.c_str(), mode);"),
    ("src/platform/common/PathOpen.h", "return std::ifstream(pathUtf8, mode);"),
    ("src/platform/AppPaths.h", "return std::fopen(utf8.c_str(), mode);"),
    ("src/audio/AudioFileWriter.cpp", "return fopen(path.c_str(), mode);"),
    # sketch-check is a developer CLI that takes its own argv.
    ("src/core/sketch/SketchCheck.cpp", 'if (FILE* fp = std::fopen(path.c_str(), "wb"))'),
    ("src/core/sketch/SketchCheck.cpp", 'if (FILE* fp = std::fopen(argv[1], "wb"))'),
}


def first_arg(text, start):
   depth, j = 1, start
   while j < len(text) and depth:
      c = text[j]
      if c == "(":
         depth += 1
      elif c == ")":
         depth -= 1
         if depth == 0:
            break
      elif c == "," and depth == 1:
         break
      j += 1
   return text[start:j].strip()


def main():
   found = []
   for path in sorted((ROOT / "src").rglob("*")):
      if path.suffix not in (".cpp", ".h", ".hpp"):
         continue
      rel = path.relative_to(ROOT).as_posix()
      if rel.startswith(SKIP_DIRS) or "Tests" in path.name or "Test" in path.stem.split("_")[-1:]:
         continue
      text = path.read_text(encoding="utf-8", errors="replace")
      lines = text.splitlines()
      for m in OPEN.finditer(text):
         arg = first_arg(text, m.end())
         line_no = text.count("\n", 0, m.start()) + 1
         line = lines[line_no - 1].strip()
         if line.startswith("//") or line.startswith("*"):
            continue
         if m.group(0).startswith("std::") and "stream" in m.group(0) and (arg.startswith("const ") or arg == ""):
            continue  # a declaration such as `std::ifstream OpenIfstreamUtf8(const std::string& p)`
         if SAFE_ARG.search(arg) or (rel, line) in ALLOW:
            continue
         found.append(f"{rel}:{line_no}: {line}")
   if found:
      print("Narrow file opens (use AppPaths::FsPath / AppPaths::OpenFile, see scripts/check_utf8_paths.py):")
      print("\n".join(found))
      return 1
   print("check_utf8_paths: no narrow file opens")
   return 0


if __name__ == "__main__":
   sys.exit(main())
