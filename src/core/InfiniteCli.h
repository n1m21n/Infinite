#pragma once

// The `infinite` command of the Terminal panel (docs/plans/terminal/README.md). The app's own binary
// run as `Infinite --cli <verb> ...`, through a shim the panel puts first on the PATH. It runs before
// any window or GPU work: a verb goes to the running app over RemoteControl, or to the headless
// flags of this same binary when given a patch file.
namespace InfiniteCli
{
   int Run(int argc, char** argv);
}
