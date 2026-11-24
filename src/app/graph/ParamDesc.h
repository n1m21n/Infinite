// ParamDesc: a node's controls as a table of rows (ui-formalisation Block C, owner sign-off 2026-11-24).
// One row says what a control is - its caption, which value it edits, its range, its format, when it shows - and
// nothing about where it goes. Ui::DrawParams (app/ui/design/DrawParam.h) draws a table top to bottom through the
// same Mod* widgets the bodies called by hand, so a migrated body looks and behaves exactly as before:
//
//   using R = params::Row<ShapeNode>;
//   static const R kShapeRows[] = {
//      R::Slider<&ShapeNode::width>("width", params::Shape::width_lo, params::Shape::width_hi).Fmt("%.0f"),
//      R::SliderInt<&ShapeNode::sides>("sides", 3, 12).At(12).If([](const ShapeNode& n) { return n.sides > 0; }),
//      R::Colour<&ShapeNode::fillColor>("fill"),
//   };
//   Ui::DrawParams(n, kShapeRows);
//
// Adding a knob is one row. Order is the contract: a slider's modulation ordinal is its place in draw order (the
// running gParamCounter), so rows draw in table order and a row that only shows sometimes either takes a fixed
// ordinal (.At) or sits after every always-shown row - the same rule FixedParamOrdinal states for hand code.
// Checkboxes and dropdowns are keyed by label (DiscreteParamSlot) and colours by gColorCounter, as before.
//
// A value is reached through a member pointer (R::Slider<&Node::field>) or, for params kept in a store, a
// captureless lambda (R::Slider("delay", [](ChorusNode* n) { return n->ParamPtr("delay"); }, lo, hi)).
// Anything a row cannot say (a visualiser, a dropdown whose pick does more than store an index) is R::Custom with
// a function that draws it by hand; it still sits in the table, so the order stays in one place.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/Modulation.h" // FaderPosToValueFn / FaderValueToPosFn

namespace params
{
   enum class Kind : uint8_t
   {
      Slider,    // ModSlider
      SliderInt, // ModSliderInt
      Toggle,    // ModCheckbox
      Knob,      // AudioKnobRow::Knob (drawn by Ui::DrawKnobs into a row's cells)
      KnobInt,   // AudioKnobRow::KnobInt
      Choice,    // DropdownButton; picking stores the index
      Colour,    // ColorSwatch
      Custom,    // drawn by hand
   };

   // How a knob maps its travel to the value. Auto is AudioKnobRow's own guess from the range and format.
   enum class Taper : uint8_t { Auto, Db, Freq };

   template <class N>
   struct Row
   {
      Kind kind = Kind::Custom;
      const char* label = "";
      float* (*f)(N*) = nullptr;
      int* (*i)(N*) = nullptr;
      bool* (*b)(N*) = nullptr;
      float lo = 0.0f;
      float hi = 1.0f;
      const char* fmt = nullptr;  // nullptr: the widget's own default
      float size = 0.0f;          // slider / dropdown width or knob diameter; 0: the widget's default
      float step = 0.0f;
      FaderPosToValueFn posToValue = nullptr;
      FaderValueToPosFn valueToPos = nullptr;
      Taper taper = Taper::Auto;
      int ordinal = -1;           // -1: the running count; otherwise this fixed ordinal (FixedParamOrdinal)
      const char* name = nullptr; // what the matrix / perf picker call it, when the caption is ambiguous
      bool (*visible)(const N&) = nullptr;
      bool reserve = false;               // hidden, it still takes its running ordinal (Keep)
      bool (*live)(const N&) = nullptr;   // shown but greyed out (BeginDisabled) while this is false
      int cell = -1;                      // knob rows: the AudioKnobRow cell; -1: the row's next cell
      void (*custom)(N*) = nullptr;
      const std::vector<std::string>& (*options)() = nullptr;

      // ---- rows ----
      template <auto M> static float* MemF(N* n) { return &(n->*M); }
      template <auto M> static int* MemI(N* n) { return &(n->*M); }
      template <auto M> static bool* MemB(N* n) { return &(n->*M); }
      template <auto M> static float* MemArr(N* n) { return n->*M; }

      static constexpr Row Make(Kind k, const char* label)
      {
         Row r{};
         r.kind = k;
         r.label = label;
         return r;
      }

      template <auto M>
      static constexpr Row Slider(const char* label, float lo, float hi) { return Slider(label, &MemF<M>, lo, hi); }
      static constexpr Row Slider(const char* label, float* (*get)(N*), float lo, float hi)
      {
         Row r = Make(Kind::Slider, label);
         r.f = get;
         r.lo = lo;
         r.hi = hi;
         return r;
      }

      template <auto M>
      static constexpr Row SliderInt(const char* label, int lo, int hi) { return SliderInt(label, &MemI<M>, lo, hi); }
      static constexpr Row SliderInt(const char* label, int* (*get)(N*), int lo, int hi)
      {
         Row r = Make(Kind::SliderInt, label);
         r.i = get;
         r.lo = (float)lo;
         r.hi = (float)hi;
         return r;
      }

      template <auto M>
      static constexpr Row Toggle(const char* label) { return Toggle(label, &MemB<M>); }
      static constexpr Row Toggle(const char* label, bool* (*get)(N*))
      {
         Row r = Make(Kind::Toggle, label);
         r.b = get;
         return r;
      }

      template <auto M>
      static constexpr Row Knob(const char* label, float lo, float hi) { return Knob(label, &MemF<M>, lo, hi); }
      static constexpr Row Knob(const char* label, float* (*get)(N*), float lo, float hi)
      {
         Row r = Slider(label, get, lo, hi);
         r.kind = Kind::Knob;
         return r;
      }

      template <auto M>
      static constexpr Row KnobInt(const char* label, int lo, int hi) { return KnobInt(label, &MemI<M>, lo, hi); }
      static constexpr Row KnobInt(const char* label, int* (*get)(N*), int lo, int hi)
      {
         Row r = SliderInt(label, get, lo, hi);
         r.kind = Kind::KnobInt;
         return r;
      }

      template <auto M>
      static constexpr Row Choice(const char* label, const std::vector<std::string>& (*options)())
      {
         Row r = Make(Kind::Choice, label);
         r.i = &MemI<M>;
         r.options = options;
         return r;
      }

      template <auto M>
      static constexpr Row Colour(const char* label) { return Colour(label, &MemArr<M>); }
      static constexpr Row Colour(const char* label, float* (*get)(N*))
      {
         Row r = Make(Kind::Colour, label);
         r.f = get;
         return r;
      }

      static constexpr Row Custom(void (*draw)(N*))
      {
         Row r = Make(Kind::Custom, "");
         r.custom = draw;
         return r;
      }

      // ---- modifiers ----
      constexpr Row Fmt(const char* s) const { Row r = *this; r.fmt = s; return r; }
      constexpr Row Size(float s) const { Row r = *this; r.size = s; return r; }
      constexpr Row Step(float s) const { Row r = *this; r.step = s; return r; }
      constexpr Row Curve(FaderPosToValueFn p2v, FaderValueToPosFn v2p) const
      {
         Row r = *this;
         r.posToValue = p2v;
         r.valueToPos = v2p;
         return r;
      }
      constexpr Row Db() const { Row r = *this; r.taper = Taper::Db; return r; }
      constexpr Row Freq() const { Row r = *this; r.taper = Taper::Freq; return r; }
      constexpr Row At(int fixedOrdinal) const { Row r = *this; r.ordinal = fixedOrdinal; return r; }
      constexpr Row Named(const char* s) const { Row r = *this; r.name = s; return r; }
      constexpr Row If(bool (*shown)(const N&)) const { Row r = *this; r.visible = shown; return r; }
      // While hidden, the row still uses up its place in the running count, so the rows after it keep their
      // ordinals whether it shows or not (the hand form was a bare gParamCounter++ in the other branch).
      constexpr Row Keep() const { Row r = *this; r.reserve = true; return r; }
      constexpr Row LiveIf(bool (*isLive)(const N&)) const { Row r = *this; r.live = isLive; return r; }
      constexpr Row Cell(int index) const { Row r = *this; r.cell = index; return r; }
   };
}
