// The node clipboard as text: the selection written in the patch format, so a copy made in one patch or one window
// can be pasted into another. The in-process fast path in StageKeyboard.cpp stays for same-patch copy and paste.
#pragma once
#include <set>
#include <string>
#include <vector>
#include "imgui.h"

namespace app
{
   // "infinite-nodes v<N>\n" then the patch text of the chosen nodes. Empty when none of the indices exist.
   // Carries only what lives between the copied nodes: params, cables, modulation, palette links, expressions.
   std::string NodeClipboardSerialize(const std::set<int>& indices);

   bool LooksLikeNodeClipboard(const std::string& text);

   struct NodePasteResult
   {
      bool ok = false;
      int pasted = 0;
      int skipped = 0;            // node types this build does not have
      std::string message;        // what to tell the user; calm for a refusal
      std::vector<int> newIndices;
   };

   // Validates the header and version, then adds the nodes with fresh indices and uids, centred on `at` (canvas
   // space), as one undo step. Nothing is touched when the text is refused.
   NodePasteResult NodeClipboardPaste(const std::string& text, const ImVec2& at);
}
