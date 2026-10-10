// Names an undo step by comparing the patch before and after it. Used when a call site did not pass a label,
// so every history row says what changed ("amount 0.20 -> 0.55", "Add Blur", "Move 3 nodes") instead of "Edit".
// Pure function of two Patch::Data; labels are never written into patches.
#pragma once
#include "core/Patch.h"
#include <string>

namespace UndoDescribe
{
   std::string Change(const Patch::Data& before, const Patch::Data& after);
}
