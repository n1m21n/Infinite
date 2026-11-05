#pragma once
// Parent and liveness of a process by id, so `infinite` can find the app whose Terminal it was started
// from even when an agent's tool shell dropped the environment the Terminal set.

namespace ProcessTree
{
   long Self();
   // 0 when the process is gone or its parent cannot be read.
   long ParentPid(long pid);
   bool Alive(long pid);
}
