// DrawParam: draws params::Row tables (app/graph/ParamDesc.h) through the Mod* widgets, and registers them with
// Modulation without drawing (ui-formalisation Block C).
//
// Pixel rule: every row makes exactly the call the hand-written body made (same widget, same arguments, same
// defaults), so a migrated body draws the same pixels, the same ImGui IDs and the same modulation ordinals.
//
//   Ui::DrawParams(n, kShapeRows);              // a stacked body: every row, top to bottom
//   Ui::DrawParam(n, kShapeRows[3]);            // one row, where hand code still sits around it
//   Ui::DrawKnobs(row, n, kChorusRows);         // knob rows into an AudioKnobRow's cells, left to right
//   Ui::RegisterParams(n, nodeIndex, kRows);    // ParamRefs for every shown slider, no ImGui at all
#pragma once
#include "app/AppShared.h"
#include "app/graph/ParamDesc.h"

#include <cstddef>
#include <functional>

namespace Ui
{
   namespace detail
   {
      // A fixed ordinal for the row's draw, the running count restored after (FixedParamOrdinal's rule).
      struct RowOrdinal
      {
         explicit RowOrdinal(int ordinal) : saved(app::gParamCounter), fixed(ordinal >= 0)
         {
            if (fixed)
               app::gParamCounter = ordinal;
         }
         ~RowOrdinal()
         {
            if (fixed)
               app::gParamCounter = saved;
         }
         RowOrdinal(const RowOrdinal&) = delete;
         RowOrdinal& operator=(const RowOrdinal&) = delete;
         int saved;
         bool fixed;
      };

      // False when the row is hidden; a hidden Keep() row still uses up its running ordinal.
      // A Choice row's current index and its store (an int member, or a float param rounded the way the audio
      // bodies read it: (int)(v + 0.5f)).
      template <class N>
      int ChoiceCurrent(N* n, const params::Row<N>& r)
      {
         return r.i != nullptr ? *r.i(n) : (int)(*r.f(n) + 0.5f);
      }

      template <class N>
      std::function<void(int)> ChoiceStore(N* n, const params::Row<N>& r)
      {
         int* (*gi)(N*) = r.i;
         float* (*gf)(N*) = r.f;
         const bool undo = r.undo;
         return [n, gi, gf, undo](int v) {
            if (undo)
               app::PushUndoCheckpoint();
            if (gi != nullptr)
               *gi(n) = v;
            else
               *gf(n) = (float)v;
         };
      }

      template <class N>
      bool Shown(const N* n, const params::Row<N>& r)
      {
         if (r.visible == nullptr || r.visible(*n))
            return true;
         if (r.reserve && r.ordinal < 0)
            app::gParamCounter++;
         return false;
      }

      // Greys the row out (BeginDisabled) while its LiveIf test is false. Only while drawing: under
      // gParamRegisterOnly there may be no ImGui window at all, and the widget it wraps isn't drawn anyway.
      struct RowLive
      {
         explicit RowLive(bool live) : off(!live && !app::gParamRegisterOnly)
         {
            if (off)
               ImGui::BeginDisabled();
         }
         ~RowLive()
         {
            if (off)
               ImGui::EndDisabled();
         }
         RowLive(const RowLive&) = delete;
         RowLive& operator=(const RowLive&) = delete;
         bool off;
      };
   }

   // One row, drawn where the call sits. Returns what the widget returned (true on a change for sliders and
   // toggles; false for rows that report nothing).
   template <class N>
   bool DrawParam(N* n, const params::Row<N>& r)
   {
      using params::Kind;
      if (!detail::Shown(n, r))
         return false;
      detail::RowOrdinal at(r.ordinal);
      detail::RowLive live(r.live == nullptr || r.live(*n));
      const float w = r.size > 0.0f ? r.size : app::kParamWidth;
      switch (r.kind)
      {
      case Kind::Slider:
         return app::ModSlider(r.label, r.f(n), r.lo, r.hi, r.fmt != nullptr ? r.fmt : "%.3f", w, false, r.step,
                               r.posToValue, r.valueToPos, -1, r.name);
      case Kind::SliderInt:
         return app::ModSliderInt(r.label, r.i(n), (int)r.lo, (int)r.hi, w, false, r.fmt != nullptr ? r.fmt : "%.0f");
      case Kind::Toggle:
         return app::ModCheckbox(r.label, r.b(n));
      case Kind::Choice:
         app::DropdownButton(r.label, r.Options(), detail::ChoiceCurrent(n, r), detail::ChoiceStore(n, r), w, r.caption,
                             r.segmented);
         return false;
      case Kind::Colour:
         app::ColorSwatch(r.label, r.f(n), n);
         return false;
      case Kind::Custom:
         r.custom(n);
         return false;
      case Kind::Knob:
      case Kind::KnobInt:
         IM_ASSERT(false && "knob rows draw into an AudioKnobRow: Ui::DrawKnobs");
         return false;
      }
      return false;
   }

   template <class N, size_t K>
   void DrawParams(N* n, const params::Row<N> (&rows)[K])
   {
      for (const params::Row<N>& r : rows)
         DrawParam(n, r);
   }

   // Knob rows into the row's cells, left to right from its current index. Custom rows get the node and may
   // place their own cell (row.index, row.Dropdown ...) through a captured row - see AddSyncedRateCell.
   template <class N>
   void DrawKnob(app::AudioKnobRow& row, N* n, const params::Row<N>& r)
   {
      using params::Kind;
      if (!detail::Shown(n, r))
         return;
      if (r.cell >= 0)
         row.index = r.cell;
      detail::RowOrdinal at(r.ordinal);
      detail::RowLive live(r.live == nullptr || r.live(*n));
      const float dia = r.size > 0.0f ? r.size : app::kKnobSmall;
      switch (r.kind)
      {
      case Kind::Knob:
         row.Knob(r.label, r.f(n), r.lo, r.hi, r.fmt != nullptr ? r.fmt : "%.3f", dia, r.taper == params::Taper::Db,
                  r.taper == params::Taper::Freq, app::AudioWidgetStyle::Knob, r.posToValue, r.valueToPos, -1, r.name);
         return;
      case Kind::KnobInt:
         row.KnobInt(r.label, r.i(n), (int)r.lo, (int)r.hi, dia);
         return;
      case Kind::Choice:
         row.Dropdown(r.label, r.Options(), detail::ChoiceCurrent(n, r), detail::ChoiceStore(n, r));
         return;
      case Kind::Toggle:
         row.Checkbox(r.label, r.b(n));
         return;
      default:
      {
         // Not a knob: drawn as a stacked row (the hand form for these is the row's own helpers - Dropdown,
         // Checkbox - which stay hand-written around DrawKnobs). Undo the outer ordinal / grey-out so DrawParam
         // applies them once.
         params::Row<N> plain = r;
         plain.ordinal = -1;
         plain.live = nullptr;
         DrawParam(n, plain);
         return;
      }
      }
   }

   template <class N, size_t K>
   void DrawKnobs(app::AudioKnobRow& row, N* n, const params::Row<N> (&rows)[K])
   {
      for (const params::Row<N>& r : rows)
         DrawKnob(row, n, r);
   }

   // The ParamRefs a table's float rows would register, without ImGui: what Modulation::KnownParam holds for a
   // node whose body never drew (collapsed, off screen, headless). Ordinals follow the draw's rule exactly - the
   // running count in table order, fixed rows at their own, hidden rows skipped - so a binding made on the drawn
   // control and one made from here point at the same param. Discrete rows (toggles, choices) are keyed by label
   // and colours by their own counter; they are not ParamRef floats and are left to the draw.
   template <class N, size_t K>
   void RegisterParams(N* n, int nodeIndex, const params::Row<N> (&rows)[K], int firstOrdinal = 0)
   {
      using params::Kind;
      int running = firstOrdinal;
      for (const params::Row<N>& r : rows)
      {
         if (r.visible != nullptr && !r.visible(*n))
         {
            if (r.reserve && r.ordinal < 0)
               running++;
            continue;
         }
         if (r.kind != Kind::Slider && r.kind != Kind::SliderInt && r.kind != Kind::Knob && r.kind != Kind::KnobInt)
            continue;
         if (r.kind == Kind::SliderInt || r.kind == Kind::KnobInt)
         {
            // Int rows register through their own widgets (a float mirror of the int); counted, not registered here.
            if (r.ordinal < 0)
               running++;
            continue;
         }
         ParamRef ref;
         ref.nodeIndex = nodeIndex;
         ref.paramIndex = r.ordinal >= 0 ? r.ordinal : running++;
         ref.value = r.f(n);
         ref.minValue = r.lo;
         ref.maxValue = r.hi;
         ref.step = r.step;
         ref.name = r.name != nullptr ? r.name : r.label;
         ref.posToValue = r.posToValue;
         ref.valueToPos = r.valueToPos;
         Modulation::Instance().RegisterParam(ref);
      }
   }
}
