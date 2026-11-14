# Исследование: UI параметров ноды (под боковой инспектор)

Только чтение кода (клон на коммите `3207e41`). Ничего не собиралось и не запускалось. Номера строк — на момент чтения. Нестабильные якоря лучше искать по имени символа.

## 1. UI-стек и главный цикл

- **Dear ImGui + imgui-node-editor** (`ed::`), OpenGL, GLFW. См. `CMakeLists.txt:391-392` (`external/imgui`, `external/imgui-node-editor`), `:904-912`. Собственного рендера виджетов нет: все контролы — обёртки над ImGui и `ImDrawList`.
- Дизайн-слой: `src/app/ui/design/components/*.h` (`SectionCard`, `PanelFrame`, `ActionButton`, `GlyphToggle`, `PinDot`, `NodeHeader`, `FormParts`…) и токены `tok::`. Обёртки параметров: `src/app/ui/ModWidgets.cpp`, `src/app/ui/ParamWidgets.cpp`.
- **Главный цикл** — `main()` в `src/main.cpp:~45-75` (170 строк; ARCHITECTURE.md про «105k строк в main.cpp» устарел, main разрезан на стадии). Порядок кадра: `DrawFramePump → DrawMenuBar → DrawLayout → … → DrawNodeBodies → DrawLinks → DrawKeyboard → DrawPopupsA/B → DrawSidePanels → DrawFloating → DrawPresent` (стадии в `src/app/frame/Stage*.cpp`, общий контекст `FrameCtx`, `src/app/frame/FrameCtx.h`).
- **Канвас**: `ed::Begin("graph", …)` в `src/app/frame/StageLayout.cpp:440` (внутри `DrawLayout`). Ноды рисуются в `DrawNodeBodies` (`src/app/frame/StageNodeBodies.cpp:37`), кабели — в `DrawLinks` (`StageLinks.cpp`), `ed::End()` — дальше по стадиям. Док-панели считаются там же: `StageLayout.cpp:98-110` (флаги `viewportRight/matrixRight/perfRight/arrangeRight`), `:401-412` (`rightReserved` → `graphWidth`).
- Глобальное состояние — десятки глобалов в `src/app/AppState.cpp` / `AppShared.h` (6.5k строк), `gEditor` (`AppState.cpp:70`), `gNodes` (вектор `GraphNode`).

## 2. Как рисуется нода

Всё одним большим циклом `for (GraphNode& gn : gNodes)` в `DrawNodeBodies` (`StageNodeBodies.cpp:~100-1510`, ~1400 строк в одной функции). Отдельного класса «NodeView» нет.

Структура тела (в порядке рисования):
1. Стиль/цвет: тинт по категории (`CategoryColors::ColorFor(gn.category)`, `:~160-210`), красная/янтарная рамка при `ICookWarningSource`/`gLiveIssues`.
2. `ed::BeginNode(gn.NodeId())` — `:216`; `gInsideNodeCanvas = true`.
3. **Входные пины** по верхнему краю (`:~230-320`), `DrawPin(...)`. Широкие ноды (Render3D, Material) переносят пины в несколько строк.
4. **Заголовок** (`:~320-358`): `NodeTitle(gn)`, `#N` инстанса, категория, бейдж проблемы (`NodeHeader::*`).
5. `BeginNodeParams(gn.index)` — `:368` (`ParamWidgets.cpp:15`): обнуляет счётчики `gParamCounter/gColorCounter/gDiscreteParamCounter`, ставит `gCurrentNodeIndex`.
6. **Превью/тело** (`:~370-700`): картинка (`DrawPreview`, `PreviewBodies.cpp:612`), мини-3D-вьюпорт (`DrawMiniViewport`, `ViewportPanel.cpp:18`), статбокс, у аудио-нод — `DrawAudioNodeBody(gn)` (`:671`, `AudioNodeBodies.cpp:633`).
7. **Ряд тоглов** (`:~735-815`): глазок `EyeToggle`, bypass, мини-вьюпорт, теги `mod`/`pal`.
8. **Параметры** (`:~855-1480`): цепочка `dynamic_cast` → `DrawXxxParams(n)` (см. п.3).
9. `ImGui::EndGroup()` → `contentW` (`:~1427`), затем **выходные пины** справа снизу (`:~1445-1500`), `ed::EndNode()` (`:1508`), `CacheNodeWidth(...)`.

**Размер ноды не считается явно** — его определяет сам ImGui/node-editor по содержимому группы. Управляется константами и ширинами виджетов: `kPreviewSize=190`, `kViewportSize=340`, `kParamWidthBase=168` (живая `gParamWidthLive`, подгоняется под тело, «R3»), `kWideNodeWidth=476` (`AppShared.h:16-65`); для аудио — `AudioNodeWidth(node)` (`AppShared.h:3616`), для MPC — `MpcNodeWidth()`. Ширина прошлого кадра кэшируется `CacheNodeWidth` (`AppShared.h:~4370`) — некоторые ноды центрируются по ней.

Пины: id считаются арифметикой от индекса — `GraphNode::NodeId/InputPinId/ParamPinId/ColorPinId/OutputPinId`, `kStride`, `kParamBase`, `kColorBase`, `kOutputBase` (`src/core/GraphNode.h:~112-135`).

Culling: невидимые ноды пропускаются (`ed::KeepOffscreenNodeAlive`, `StageNodeBodies.cpp:~143`), кроме `IsParamDriven()` / открытых popup / каждого `kCullRefresh`-го кадра — т.е. **тело параметров видимых нод рисуется каждый кадр** и вне экрана иногда тоже.

## 3. Модель параметров (главный вопрос)

**Единого декларативного реестра параметров нет.** Есть три разных слоя, и они не связаны:

| Слой | Где | Что делает |
|---|---|---|
| Сохранение/загрузка | `INode::VisitParams(ParamVisitor&)` (`src/core/INode.h:83-92, 211`); методы `Float/Int/Bool/Text/Color(name, ref)` | Только ключ→значение для патча (`Patch.cpp`, `PatchJson.cpp`), снапшот для кэша cook. Диапазонов, групп, подписей, виджета здесь **нет**. |
| Модуляция | `ParamRef` (`src/core/Modulation.h:61-91`): `nodeIndex, paramIndex, float* value, min, max, step, name, isEnum/isBool, enumOptions, srcAddr` | Заполняется **самим виджетом** в момент рисования: `ModSlider`/`ModKnob`/`ModSliderInt`/`ModKnobInt` вызывают `Modulation::RegisterParam(ref)` (`ModWidgets.cpp:95-121`, `:1194-1297`). Это и есть «реестр» диапазонов/имён, но он существует только пока виджет вызван. |
| Отрисовка | десятки `DrawXxxParams(Node*)` / `DrawXxxBody(gn,n)` | Ручной императивный код: `ModSlider("low", &n->low, 0, 1)`, `DropdownButton("shape", …)`, `ImGui::BeginDisabled(...)`. Группы — `NodeSeparator(label)`/`SectionCard`. Локализация — `T("…")`/`L("…")` (`src/core/I18n.h:42`) точечно, не системно. |

Идентичность параметра для модуляции — **порядковый номер вызова виджета** (`paramIndex = gParamCounter++`, `ModWidgets.cpp:104`), не имя. Следствия: порядок/условное рисование виджетов нельзя менять без `FixedParamOrdinal` (`AppShared.h:1809`) и `RegisterHiddenParam(...)`; дискретные (bool/enum) идут с отдельной базы `kDiscreteParamBase` и хэшем метки (`DiscreteParamSlot`, `BrowserUi.cpp:335`, с таблицей алиасов переименований). «Ключ сохранения ↔ ordinal» сопоставляется через `ParamRef::srcAddr` и `--describe` (`Modulation.h:83-87`).

### Доля декларативного механизма (оценка)

- Типов нод ≈ 347 в меню: ~205 `REGISTER_NODE` (`src/app/graph/NodeRegistry.cpp`, 205 вхождений) + 49 `FilterDef` + 28 `EffectDef` + варианты (ShapeNode/GeometryNode: `CreateFor(i)`, циклы на `:22-96`). Классов с собственным `Draw*` — порядка 170 (веток в диспетчере см. ниже; аудит `docs/node_param_audit.md`: 201 тип, 1305 модулируемых контрола).
- **Декларативные таблицы** (параметры описаны данными): `FilterDef/FilterParamDef` (`src/core/FilterDefs.h:10-70`: label, uniform, type Float/Int/Bool/Color/Enum, min/max, default, options, sectionLabel, isDegrees, format, integer, legacy) — 49 нод; `EffectDef/EffectParamDef` (`src/audio/EffectDefs.h:38-130`: name, min, max, default, uiOnly, prereqs) — 28 аудио-эффектов; `GeometryOpNode` (одна кл., выбор операции из списка; **определение не проверено**). Итого **≈ 80 из ~350 типов (~22%)** описывают параметры данными.
- **Но и они рисуются не чисто по описанию.** `DrawFilterParams` (`GeometryParams.cpp:2786`) — общий цикл по `def.params`, однако с хардкодом по имени фильтра (`def.name == "color adjustments"` — складка «show more» через `showAdvancedParams`; `"convolve"` — 3×3 матрица, `:2795, 2837`). У `EffectDef` описание параметров не содержит виджета: у каждого эффекта своё `DrawXBody` с визуализатором (`EffectVisualizerId`, ~28 вариантов, диспетчер `AudioNodeBodies.cpp:770+`; напр. `DrawAudioFilterBody` `FxBodies1.cpp:434`).
- **Кастомный рисующий код** — всё остальное, ~85-90% типов.

Примеры **декларативных** (описание в данных, UI — общий цикл + хаки):
1. `FilterNode` ×49 («blur», «motionblur», «color adjustments», «convolve»…) — `FilterDefs.cpp:114+`.
2. `AudioEffectNode` ×28 (Audio Filter, Dynamics, Delay, Reverb, EQ…) — `EffectDefs.cpp:37+` (UI всё равно ручной per-effect).
3. `ConstantNode` — одна строка `ModSlider("value", …)` прямо в диспетчере (`StageNodeBodies.cpp:~968`).
4. `GeometryOpNode` (`DrawGeometryOpParams`, `GeometryParams.cpp:1507`) — выбор операции из dropdown (не проверено, насколько таблично).

Примеры **кастомного** кода:
1. `LFONode` — `DrawLFOParams` (`ParamBodies1.cpp:907`): dropdown, `DrawModRateControls`, слайдеры, условный `seed` с `FixedParamOrdinal(5)` / `RegisterHiddenParam`.
2. `FieldElement/Primitive/Pixel/Sample/Synth/Notes/Graph` — `FieldParams.cpp:251, 594`, редактор кода, пресеты, ошибки компиляции.
3. `CurvesNode` — `DrawCurvesParams` (`ParamBodies2.cpp:145`), редактор кривой.
4. `DrumSequencerNode` — `DrawDrumSequencerBody` (`SamplerBodies.cpp:1871`), 8 дорожек, степ-сетка.
5. `SamplerNode` — `DrawSamplerBody` (`SamplerBodies.cpp:340`), форма волны.
6. `GeometryTableNode` — `DrawGeometryTableParams` (`GeometryParams.cpp:314`), сетка с собственными пинами строк.
7. `VmpcNode` — `DrawVmpcBody` (`VmpcBody.cpp:171`), пэды MPC (ширина `MpcNodeWidth`).
8. `OutputNode` — инлайн-блок прямо в диспетчере (`StageNodeBodies.cpp:~1135-1425`, настройки записи/оффлайн-рендера, ~300 строк).

### Диспетчер

`StageNodeBodies.cpp:~858-1480`: две цепочки `dynamic_cast → DrawXxxParams(n)` (разбита надвое из-за лимита MSVC на вложенность, `:1001`), ~113 + ~65 веток. Для аудио-нод — отдельный `DrawAudioNodeBody` (`AudioNodeBodies.cpp:633`, ~63 ветки + switch по `visualizerId`). Сигнатура у `Draw*Params` почти везде `void(NodeT*)`, у части `(GraphNode&, NodeT*)` (`DrawDriftParams`, `DrawXBody`). **Функции рисуют в «текущее ImGui-окно» и неявно опираются на глобалы** (`gCurrentNodeIndex`, `gParamCounter`, `gInsideNodeCanvas`, `gParamWidthLive`).

## 4. «Глазок» (раскрытие параметров)

- Флаг: `GraphNode::showParams` (`src/core/GraphNode.h:51`, по умолчанию `false` — «чтобы превью вело»). Отдельный флаг `showAdvancedParams` (`:~68`) — складка «show more» внутри некоторых нод (Filter «color adjustments»). И `showMiniViewport`.
- Кнопка: `EyeToggle(bool)` → `GlyphToggle::Draw("##eye", …)` (`ModWidgets.cpp:1792`), вызывается в `StageNodeBodies.cpp:755`; клик инвертирует `gn.showParams`.
- Сохранение: текстовый формат `flags <showParams> <bypassed> <showMiniViewport> <showAdvancedParams>` (`src/core/Patch.cpp:299`, чтение `:752`, схема `Patch.h:26,159`); JSON — `PatchJson.cpp:21` (`jn["showParams"]`). **Состояние раскрытия лежит в патче** → при внедрении инспектора старые патчи будут с `showParams=true`.
- Влияние на отрисовку: `paramsOpen = gn.showParams` (`StageNodeBodies.cpp:157`). Диспетчер `Draw*Params` вызывается при `paramsOpen || registerOnlyParams` (`:~858`) — размер ноды растёт ровно на высоту этого блока. Макро-ноды принудительно `showParams=false` (`:726`) и без глазка; комментарий-ноды и **аудио-ноды глазка не имеют** — их тело (`DrawAudioNodeBody`) всегда развёрнуто, `showParams` для них не используется (`:710-715`, `GraphNode.h:60`). Загрузка/Startup-тесты массово выставляют `showParams=true` (`Startup.cpp:2166…3620`).
- Когда свёрнуто и есть что-то «привязанное», рисуются теги `mod`/`mod±`/`pal` (`:~770-810`).

## 5. Точки модуляции

- Пин — это **часть самого виджета**, а не отдельный слой. `ModSlider` внутри себя делает `ed::BeginPin(pinId, PinKind::Input)` + `PinDot::Param(...)` слева от слайдера (`ModWidgets.cpp:~135-160`), кладёт id в `gDrawnParamPins`, запись `{nodeIndex, paramIndex, typeName, name, центр, rect}` в `gParamPinScreenList` (для режима «Assign» и перфомансa — `StagePopupsB.cpp:~896`). Те же приёмы в `ModKnob`, `DrawDiscreteParamPin` (`ParamWidgets.cpp:715`) и ещё ~20 мест `ed::BeginPin` в `src/app`. Правый клик/hover → `DrawModulationBindingMenu`.
- **Свёрнутая нода (важно — механизм уже есть):**
  - Параметры не «умирают»: при `!paramsOpen` и наличии привязок (`gn.IsParamDriven()`) тот же диспетчер `Draw*Params` всё равно вызывается в режиме **`gParamRegisterOnly`** (`StageNodeBodies.cpp:~838-865`): `Mod*` виджеты делают `RegisterParam` и сразу `return false` (`ModWidgets.cpp:122, 1298, 1624`), `SkipItems=true` гасит прочие ImGui-виджеты, пустой clip rect — draw-list. Без этого закрытый глаз замораживал бы все модуляторы (комментарий `:~828-845`).
  - Кабели к скрытым пинам: `CollapsedBindingPins` (`ModWidgets.cpp:1699`) — невидимые 1-px stub-пины на теге `mod`/`pal`, чтобы кабель не умер (`ed::` убивает линк без объявленного пина).
  - Запасной путь в `DrawLinks`: если пин не нарисован (`gDrawnParamPins.count(pin)==0`), кабель рисуется пунктиром до якоря `gPinAnchors`/`gPinAlias` (`StageLinks.cpp:~176-200`).
- Для инспектора это ключевое: визуально «кабель к параметру» привязан к `ed::` пину внутри `ed::BeginNode`; вне канваса `ed::BeginPin` вызвать нельзя.

## 6. Кастомные встроенные UI (не сводятся к списку параметров)

- **Превью**: текстура/чекерборд (`DrawPreview`, `PreviewBodies.cpp:612`), рисуемый холст `DrawPaintablePreview` (`:17`), мини-3D вьюпорт `DrawMiniViewport` (`ViewportPanel.cpp:18`), Comment (`DrawCommentPreview`, `:94`), Group-нода со своим `ed::BeginNode` (`DrawGroupNode`, `:446`).
- **Кривые/огибающие**: `DrawCurvesParams` (`ParamBodies2.cpp:145`), `DrawMSEGParams` (`:403`) + отдельная панель `CurveEditorPanel.cpp` (2755 строк, с **собственным `DrawInspector`**, `:2103`), ModCurve.
- **Секвенсоры/сетки**: `DrawDrumSequencerBody` (`SamplerBodies.cpp:1871`), `DrawNoteSequencerBody`, `DrawMacroStepGateParams` (`ParamBodies1.cpp:1887`), `DrawMacroXYParams` (`:2008`), `DrawPianoRollBody` (`NoteBodies.cpp:2081`) + `PianoRollPanel.cpp`, VMPC, `StepCell`.
- **Редакторы кода**: Formula (`ParamBodies1.cpp:388`), Shadertoy, Sketch/Sketch3D (`SketchParams.cpp`), Field* (`FieldParams.cpp`).
- **Осциллограммы/визуализаторы**: `DrawWavetableScope`, `DrawFieldSampleScope`, `DrawFieldSynthScope`, `DrawFieldNotesRoll` (`AppShared.h:3618-3623`), `DrawFieldGraphWaveform` (`PreviewBodies.cpp:757`), 28 визуализаторов эффектов (`EffectVisualizerId`), форма волны сэмплера, `AudioViz.h`.
- **Прочее**: палитра (`DrawPaletteParams` `ParamBodies2.cpp:656`), таблица геометрии, OSC/MIDI-статусы, оффлайн-рендер в `OutputNode`.
- Часть таких блоков уже имеет «большой» вариант в док-панели (Piano Roll, Sample Studio, Curve Editor, 3D View) — они вынесены из ноды и следуют за выделением.

## 7. Есть ли что-то похожее на инспектор

Универсального нет, но есть **прецеденты, которые надо смотреть первыми**:
- **`CurveEditorPanel::DrawSide`** (`CurveEditorPanel.cpp:~2305`) — «колонка свойств» выбранного MSEG: `BeginChild` с `PushDockedPanelStyle`, заголовок + крестик, `SectionCard`-карточки (`DrawInspector`, `:2103`). Следует за выделением через `FollowCanvasSelection` (`:~265`, `ed::IsNodeSelected`, временно `ed::SetCurrentEditor(gEditor)`). Лучший образец «inspector вне канваса».
- `View3DPanel::FollowCanvasSelection` (`View3DPanel.cpp:~285-315`), `PianoRollPanel.cpp:508` — тот же паттерн выделения.
- **Док-инфраструктура правой/нижней колонок**: `gViewportPanelDock`, `gModMatrixDock`, `gPerfPanelDock` (`AppState.cpp:442`), расчёт в `StageLayout.cpp:98-110, 401-412`, отрисовка `StageSidePanels.cpp` (напр. `DrawViewportPanelDocked`, `:380, 705`). Сюда добавляется ещё одна колонка.
- `ModMatrixPanel.cpp` — обзор привязок модуляции; Perf-панель имеет «Assign Parameter» пикер по `FrameParams()`.
- Панель `##nodepanel` (`StageSidePanels.cpp:423`, ширина `kNodePanelWidth=360`) — это **браузер нод**, не инспектор.

## Выводы для реализации инспектора

**Что можно переиспользовать почти как есть**
- Сами `DrawXxxParams(Node*)` — это обычный ImGui в «текущем окне»; вызвать их внутри `BeginChild` боковой панели технически можно, без правки тел. Нужно лишь выставить глобалы так же, как это делает нода: `BeginNodeParams(gn.index)`, `gParamWidthLive`, и т.п. Диспетчер (цепочку `dynamic_cast`) придётся **вынести в функцию** `DrawNodeParamsBody(GraphNode&)` и вызывать из обоих мест (сейчас она — кусок 1400-строчного цикла, `StageNodeBodies.cpp:~858-1480`).
- Прецедент панели: `CurveEditorPanel::DrawSide` + `FollowCanvasSelection` + док-флаги `StageLayout`. Карточки: `SectionCard`, `PanelFrame`.
- Режим **`gParamRegisterOnly`** уже решает «параметры не нарисованы, но живы для модуляции» — это ровно состояние ноды на канвасе, когда инспектор владеет UI параметров.
- Виджеты `ModSlider/ModKnob/Dropdown/NodeCheckbox` — модулируемость, ввод значения, выражения, undo, hotkeys общие; в панели работать будут.

**Что придётся рефакторить**
1. **Двойной вызов тела параметров.** Если параметры рисуются в инспекторе, на канвасе тот же `Draw*Params` нужно гонять в `gParamRegisterOnly` (иначе остановятся модуляторы/expressions, ParamRef не зарегистрируются). Значит: на канвасе всегда `registerOnly`-проход (с дороговизной по CPU — сейчас он выполняется только при привязках; без них для каждой ноды лишний проход — **измерить**), а в инспекторе — полный проход для **одной** выделенной ноды. Два прохода одной ноды в кадре ломают `gParamCounter`/`gDrawnParamPins` — нужна явная изоляция счётчиков (`BeginNodeParams` перед каждым).
2. **Пины внутри виджетов (`ed::BeginPin` в `ModSlider`/`ModKnob`/`DrawDiscreteParamPin`).** В боковой панели (вне `ed::Begin/End`) `ed::BeginPin` вызывать нельзя. Нужен флаг контекста: «inspector mode» — виджет рисует точку-индикатор как обычный ImGui-элемент (drop-target для кабеля/клик → меню `DrawModulationBindingMenu`), а пин объявляется как stub-пин на ноде (по аналогии с `CollapsedBindingPins`, `ModWidgets.cpp:1699`). Расширить `CollapsedBindingPins` на все параметры с привязками (сейчас — только bound). Перетягивание кабеля мышью на параметр в инспекторе — отдельная задача (нужен собственный drag-and-drop или тянуть к ноде и выбирать параметр из меню — ср. `gParamPinScreenList` / Assign-режим).
3. **Ширины/раскладка.** Тела рассчитаны на `kParamWidth=168`, `kPreviewSize`, на фиксированные ширины нод (`AudioNodeWidth`, `MpcNodeWidth`, `WideNodeCentreOffset`, `bodyWidth` в `EffectDef`). В панели шириной ~360 нужна подстановка `gParamWidthLive` и проверка каждого кастомного тела. Параметры-колонки внутри тел (knob-ряды `AppShared.h:~3098`) — см. п.6.
4. **Глобалы вместо контекста.** Тела читают/пишут `gCurrentNodeIndex`, `gInsideNodeCanvas`, `gParamCounter`, `gNodeHoverTip`… Часть кода проверяет `gInsideNodeCanvas`/`ed::` (например тултипы с `ed::Suspend`, `ModWidgets.cpp:~1819`, комментарии в `ParamWidgets.cpp:513`, `AudioNodeBodies.cpp:106` — «draw inside ed::Begin, не BeginCombo»). В инспекторе эти обходы не нужны/могут вести себя иначе; **нужно прогнать все 170 тел** (что это возможно автоматом — есть `gHeadlessProbeAll`, `docs/node_param_audit.md`, `scripts/audit_node_params.py`).
5. **Нет метаданных.** Единого декларативного описания нет → ни авто-генерации инспектора, ни поиска/фильтра по параметрам, ни групп без ручной работы. Реалистичный путь: **не** переписывать 170 тел, а «вынести как есть» (этап 1), а дальше эволюция: `FilterDef`/`EffectDef` (≈80 типов) → общий декларативный путь.
6. **Размер ноды на канвасе.** Что считать «компактным» телом: `Preview + заголовок + пины + (опц.) ключевые контролы`. Аудио-ноды сейчас — это и есть тело с параметрами (нет глазка), т.е. для них потребуется отделить «визуализатор» (оставить в ноде) и «knobs» (в инспектор) — требует правки каждого `DrawXBody` (~90). Для нод с интерактивным UI в теле (секвенсор, MSEG, Curves, редакторы кода, пэды) нужно решение «остаётся в ноде / открывается в инспекторе» по типу ноды — белый список.
7. **Патч/совместимость.** `showParams`/`showAdvancedParams` в формате патча (`Patch.cpp:299`): при смене семантики решить — игнорировать на чтении или переиспользовать как «параметры раскрыты в ноде» (опциональный режим). Модуляция ключуется порядковым номером виджета — **не менять порядок/условность вызовов**.
8. **Тесты.** В репо много UI-регрессий, которые крутят `showParams=true` (`Startup.cpp`, `FrameTests*.cpp`, ~1500 строк на стадию «NodeBodies») — потребуется обновлять; `docs/ui_screenshots`, скиллы `node-ui-sweep`/`node-ui-pillars`/`audio-node-ui` (в `.claude/skills`, не читались) задают правила UI.

**Главные риски**
- Скрытая связность тел параметров с глобальным состоянием и `ed::`-контекстом (`BeginPin` внутри виджета) → самая трудоёмкая часть. Сначала нужен PoC на 2-3 нодах: `LFONode` (простой), `FilterNode` (табличный), `DrumSequencerNode`/`OutputNode` (кастомный).
- Модуляция «ключ = ordinal» — легко сломать сохранение/привязки.
- Производительность: лишний `registerOnly`-проход для всех нод и двойное рисование выделенной.
- Аудио-ноды (~90 типов) и ноды с интерактивным телом требуют разделения «визуализатор / контролы» вручную.
- Шорткаты/Tab-навигация по параметрам (`Startup.cpp:2225` — «Tab walks params, so they have to be drawn») завязаны на то, что параметры нарисованы на канвасе (**не проверено, как именно работает Tab-навигация**).

**Не проверено:** точная доля тел параметров, зависящих от `gInsideNodeCanvas`; таблично ли устроен `GeometryOpNode`; поведение Tab-навигации и «assign»-режима при выносе параметров; стоимость `registerOnly` на больших патчах; содержимое `.claude/skills/node-ui-*`.
