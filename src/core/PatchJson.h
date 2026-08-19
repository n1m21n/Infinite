#pragma once

// Patch::Data <-> JSON, for the RemoteControl get_graph() RPC. One direction
// only (Data -> JSON) - patches are still saved/loaded through the existing
// text format (Patch::Write/Read); this just lets an external tool inspect
// the live graph.

#include "Patch.h"
#include "json.hpp"

namespace PatchJson
{
   nlohmann::json ToJson(const Patch::Data& data);
}
