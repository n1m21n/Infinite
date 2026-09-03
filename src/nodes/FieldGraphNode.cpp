#include "FieldGraphNode.h"

#include "ExprGlobals.h"
#include "field/FieldGraphReconciler.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"

#include <random>
#include <set>
#include <sstream>
#include <unordered_map>

std::string FieldGraphNode::NewUid()
{
   static std::mt19937_64 rng(std::random_device{}());
   uint64_t v = rng();
   static const char* kHex = "0123456789abcdef";
   std::string out(16, '0');
   for (int i = 15; i >= 0; i--)
   {
      out[i] = kHex[v & 0xF];
      v >>= 4;
   }
   return out;
}

bool FieldGraphNode::Apply()
{
   std::vector<Field::Token> tokens;
   Field::FieldError err;

   if (!Field::Lex(code, tokens, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      mHasCompiledProgram = false;
      return false;
   }

   Field::AstNodePtr astProgram;
   if (!Field::ParseProgram(tokens, astProgram, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      mHasCompiledProgram = false;
      return false;
   }

   Field::GraphIRProgram irProgram;
   if (!Field::LowerGraphProgramToIR(astProgram, irProgram, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      mHasCompiledProgram = false;
      return false;
   }

   mParamTable.Reconcile(irProgram.declaredParams, mNodeIndex, mNotice);

   mLastProgram = std::move(irProgram);
   mHasCompiledProgram = true;
   mLastError.clear();
   return true;
}

bool FieldGraphNode::Regenerate(Field::IFieldGraphHost& host)
{
   if (!Apply())
      return false;

   std::map<std::string, float> params = mParamTable.ValueMap();

   Field::GraphPlan plan;
   Field::FieldError err;
   if (!Field::InterpretGraphProgram(mLastProgram, params, ExprGlobals::Values(), plan, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   std::vector<Field::ReconcileAction> actions;
   Field::GraphOwnershipMap localOwnership = mOwnership;
   if (!Field::ReconcileGraphPlan(plan, localOwnership, host, actions, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   // Auto-placement for anything the kernel spawned without calling place():
   // left to right by emit call-site (the part of the identity key before
   // '#', stable across regenerations - doc §5.3.1), top to bottom for
   // clones of the same call-site (e.g. a for-loop emitting N copies). Runs
   // once per key, ever - a key only appears here as Mount/Remount, so a
   // user's manual drag afterward is never fought on a later Regenerate,
   // same guarantee explicit place() already has.
   {
      std::unordered_map<std::string, int> idByKey;
      for (const auto& a : actions)
         if (a.kind == Field::ReconcileActionKind::Mount || a.kind == Field::ReconcileActionKind::Remount)
            idByKey[a.key] = a.nodeId;

      std::set<std::string> placedKeys;
      for (const auto& p : plan.places)
         placedKeys.insert(p.targetKey);

      constexpr float kAutoPlaceOriginX = 60.0f;
      constexpr float kAutoPlaceOriginY = 60.0f;
      constexpr float kAutoPlaceDX = 580.0f;      // kAudioNodeWidth (440) + margin
      constexpr float kAutoPlaceDXWide = 1080.0f; // kAudioWideWidth (960) + margin
      constexpr float kAutoPlaceDY = 260.0f;
      constexpr float kAutoPlaceDYWide = 520.0f;

      // Only Wavetable/Drum Sequencer render at kAudioWideWidth
      // (main.cpp DrawWavetableBody/DrawDrumSequencerBody) - everything
      // else fits the narrower column without visibly wasting canvas.
      auto isWide = [](const std::string& typeName) {
         return typeName == "Wavetable" || typeName == "Drum Sequencer";
      };

      std::unordered_map<std::string, float> callSiteX;
      std::unordered_map<std::string, int> callSiteCloneCount;
      float nextX = kAutoPlaceOriginX;

      for (const auto& e : plan.emits)
      {
         if (placedKeys.count(e.key))
            continue;
         auto idIt = idByKey.find(e.key);
         if (idIt == idByKey.end())
            continue; // Update: already positioned by an earlier pass

         const std::string callSite = e.key.substr(0, e.key.find('#'));
         auto xIt = callSiteX.find(callSite);
         float x;
         if (xIt == callSiteX.end())
         {
            x = nextX;
            callSiteX[callSite] = x;
            nextX += isWide(e.typeName) ? kAutoPlaceDXWide : kAutoPlaceDX;
         }
         else
         {
            x = xIt->second;
         }

         const int cloneIndex = callSiteCloneCount[callSite]++;
         const float y = kAutoPlaceOriginY + cloneIndex * (isWide(e.typeName) ? kAutoPlaceDYWide : kAutoPlaceDY);
         host.Place(idIt->second, x, y);
      }
   }

   mOwnership = std::move(localOwnership);
   ownershipText = mOwnership.ToText();
   mLastPlan = std::move(plan);

   int mounted = 0, updated = 0, remounted = 0, unmounted = 0, connected = 0;
   for (const auto& a : actions)
   {
      switch (a.kind)
      {
      case Field::ReconcileActionKind::Mount: mounted++; break;
      case Field::ReconcileActionKind::Update: updated++; break;
      case Field::ReconcileActionKind::Remount: remounted++; break;
      case Field::ReconcileActionKind::Unmount: unmounted++; break;
      case Field::ReconcileActionKind::Connect: connected++; break;
      }
   }
   int droppedMod = host.DroppedModCount();
   int detachedCables = host.DetachedCableCount();
   std::ostringstream oss;
   oss << "mounted " << mounted << ", updated " << updated << ", remounted " << remounted
       << ", unmounted " << unmounted << ", " << connected << " connections";
   if (droppedMod > 0)
      oss << "; " << droppedMod << " modulation bindings dropped";
   if (detachedCables > 0)
      oss << "; detached " << detachedCables << " cables with unmounted nodes";
   mNotice = oss.str();

   mLastError.clear();
   return true;
}

void FieldGraphNode::VisitParams(ParamVisitor& v)
{
   v.Text("code", code);
   v.Text("uid", mUid);
   v.Bool("addTriggerInput", addTriggerInput);
   std::string prevOwnershipText = ownershipText;
   v.Text("ownershipText", ownershipText);
   mParamTable.VisitParams(v);

   // Loading a patch line restores ownershipText textually; keep mOwnership
   // in sync so Regenerate() reconciles against the just-loaded map rather
   // than whatever was live before the load (mirrors mState.Transplant's
   // "restore, then let the next real pass reconcile" pattern in the other
   // three Field node types).
   if (ownershipText != prevOwnershipText)
      mOwnership = Field::GraphOwnershipMap::FromText(ownershipText);
   if (mUid.empty())
      mUid = NewUid();
}
