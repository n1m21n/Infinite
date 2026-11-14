# Меню добавления нод (фильтр по типу порта) и баг подсказок параметров

Только чтение кода; исходники не менялись. Пути — от корня репозитория.
Главная находка: **фильтрация по типу порта уже наполовину реализована** («Suggested»), а не с нуля.

## Тема A. Умная фильтрация нод по типу порта

### 1. Где меню и как строится список
- Одно общее popup `"search"` рисуется в `src/app/frame/StagePopupsB.cpp:256` (`MenuParts::BeginPopup("search")`). Оно же — Shift+N, ПКМ по фону, двойной клик и drop кабеля.
- Точки открытия `ImGui::OpenPopup("search")`:
  - ПКМ по фону: `src/app/frame/StagePopupsA.cpp:843` (`ed::ShowBackgroundContextMenu`);
  - двойной клик по пустому месту: `src/app/frame/StagePopupsA.cpp:~855`;
  - Shift+N (центр вида): `src/app/frame/StageKeyboard.cpp:244`, `:1048`;
  - **drop кабеля в пустоту**: `src/app/frame/StageLinks.cpp:621-643` (`ed::QueryNewNode`).
- Поиск есть: `LibraryParts::SearchField("nodepicker", …)` (`StagePopupsB.cpp:~280`). Матч — `NodeSearchMatches(hay, q)` по `DisplayName(имя)+DisplayName(категория)` (+ локализованный haystack), ветка `else` на `StagePopupsB.cpp:~342`, список — `fc.allTypes` (пары имя/категория).
- Категории: при пустом запросе — подменю по категориям (`GetCategories()`, сортировка по `CategoryColors::SemanticRank`, `StagePopupsB.cpp:~325`), внутри — `GetNodesInCategory`, отфильтрованные `IsUserSpawnable` (`src/app/graph/Core.cpp:195`).
- Реестр: `NodeFactory` (`src/core/NodeFactory.h`) — имя → `std::function<INode*()>` + категория; регистрация в `app::RegisterNodes()` (`src/app/graph/NodeRegistry.cpp`, ~821 строк) через `REGISTER_NODE`; есть табличные семейства (Shape/Geometry/Filter и т.п.) с одним классом на много имён.

### 2. Что меню знает о контексте
- Передаётся **глобально**, не параметром: `gLinkDragSourcePin` (`AppState.cpp:189`) и `gLinkDragSuggestions` (`AppState.cpp:191`, `AppShared.h:381-383`) + `gSpawnPos`. Исходный порт и его тип известны.
- При drop (`StageLinks.cpp:621-631`): если тянули от **выхода** — `RecommendedNodeTypesForOutput(srcNode, outIdx)` (`src/app/graph/Wiring.cpp:~1540`); если от **входа** — `RecommendedNodeTypesForPin(pin)` (`src/app/frame/StageLinks.cpp:~74`, обратная задача: ищет ноды с подходящим выходом).
- Показ: при пустом запросе сверху секция «Suggested» (`StagePopupsB.cpp:~299-318`), затем все категории **без фильтра**. При непустом запросе контекст **игнорируется** — идёт поиск по `allTypes` целиком (ни фильтра, ни приглушения).
- После выбора — **автосоединение есть** (`StagePopupsB.cpp:~380-440`): `SpawnNode` → для drag от входа `WireSpawnedNodeToPin`; для drag от выхода перебирает слоты `InputCountFor(spawned)` и первый, где `IsInputSlotCompatible` истинно, соединяет через `WireInputSlot`; один Undo откатывает и ноду, и кабель (checkpoint берёт `SpawnNode`). Затем `RebuildAudioTopology()` для аудио/нот.
- При ПКМ/двойном клике/Shift+N `gLinkDragSourcePin=-1` и список подсказок очищается (`StageKeyboard.cpp:239`) — меню обычное.

### 3. Проверка совместимости
- Не в `main.cpp` (там 170 строк), а в `src/app/graph/Wiring.cpp:771`:
  `bool IsInputSlotCompatible(GraphNode* dstNode, int slot, bool srcIsModulator, IPaletteSource* srcPalette, IGeometrySource* srcGeometry, CameraNode* srcCamera, LightNode* srcLight, bool srcIsEnvironment, bool srcIsAudioNode, bool srcIsNoteSource, bool srcIsPredictor)`; объявлена `AppShared.h:2604`. Типа-enum порта нет: «тип» источника — набор из 8 bool/указателей, которые вызывающий вычисляет `dynamic_cast`-ами (дублируется в `StageLinks.cpp:~20-30`, `StageLinks.cpp:~390`, `StagePopupsB.cpp:~395`, `Wiring.cpp:~1560`, `Rpc.cpp:714`, `Wiring.cpp:~1096`).
- Правила (порядок важен, первая подходящая ветка выигрывает): predictor → всегда false; слот аудио (`AudioInputSlot`) → только аудио; слот нот (`NoteInputSlot`) → только ноты; аудио/ноты в прочее → false; Render3D — по номерам слотов (геометрия / камера / env / свет); `GeometryInputSlot` → геометрия; Material/Displacement/SetColor → картинка (SetColor слот 2 → палитра); 3D в не-3D → false; `ModulatorInputSlot` → модулятор (кроме `ImageAnalyze`/`Track*` → картинка); иначе — картинка. «Картинка» определяется от противного: не модулятор, не палитра, не геометрия/камера/свет/аудио/ноты (`Wiring.cpp:~800`).
- Отдельно, мимо этой функции: **param-пины** (точка у параметра, модуляция) → `srcIsModulator && !IsKernelDrivenParam && PredictorBindRefusal==nullptr`; **color-пины** → `srcPalette != nullptr` (`StageLinks.cpp:~28-34`, `:378-398`). Цикл-проверки (`WouldCreateAudioCycle/NoteCycle`) — только в UI-валидации, не в подсказках.
- Неявные конверсии: фактически только то, что «модулятор» принимается слотами-модуляторами, а выход картинки — на любой «обычный» вход; аудио↔картинка/геометрия↔картинка не конвертируются. Документ: `src/core/AgentDocs.h:2543-2707` (порядок веток и тексты отказов). Типов-энамов «CV vs audio» нет.

### 4. Типы портов без экземпляра ноды
- **Нет.** `NodeFactory` хранит только имя, категорию и фабрику (`NodeFactory.h:46-58`); дескрипторов портов нет. Единственный путь, который уже используется: создать «пробный» `GraphNode probe` и опросить виртуалки (`Wiring.cpp:~1575`, `StageLinks.cpp:~76`; `PatchSchema.h:104` комментарий «via probe nodes»).
- Стоимость: ~347 `MakeNode` на каждый drop (комментарий в коде: «once per drag, not per frame»). Конструкторы могут быть тяжёлыми (GL-ресурсы, аудио-движки) — **не проверено**, замеров нет; пробы пока терпимы.
- Динамические порты: `InputCountFor` (`Wiring.cpp:347`) — лестница `dynamic_cast` + generic-цикл `AudioInputSlot/NoteInputSlot` до первого `nullptr`; у ноды число входов может зависеть от состояния (mixer, Math и пр.) — у свежего пробного экземпляра будет дефолтное число портов. Для «умного» режима это допустимо (по умолчанию-созданная нода и будет вставлена).
- Модуляционные входы (точки у параметров): число/тип параметров известны только экземпляру (рисуются в телах нод `src/app/bodies/*`); в `RecommendedNodeTypesForOutput` они **сознательно исключены** (комментарий `Wiring.cpp:~1532`: «param/colour pins nearly every node accepts»). Следствие: для выхода-модулятора фильтр показывает только ноды с `ModulatorInputSlot`; ноды без такого слота, но с param-пинами, в подсказки не попадают, хотя меню в целом их допускает.
- Если делать «правильно» (без проб): добавить в `NodeFactory::Register` кэш-дескриптор `{inputs: [PortKind], outputs: [PortKind]}`, заполняемый **один раз при старте** пробным созданием (lazy-кэш по имени), с инвалидацией не нужна (дефолтные порты неизменны); ввести enum `PortKind {Image, Geometry, Camera, Light, Env, Audio, Note, Modulator, Palette}` и функцию `Compatible(PortKind src, PortKind dstSlot)`, которую вызывает и `IsInputSlotCompatible`.

### 5. Уже есть частичная реализация?
Да: `RecommendedNodeTypesForOutput` / `RecommendedNodeTypesForPin` + секция «Suggested» + автосоединение (см. п. 2). Чего **нет**: (а) фильтр/приглушение остального списка (категории и результаты поиска показываются целиком), (б) учёт контекста при поиске по тексту, (в) param/color-пины в подсказках от выхода, (г) opacity 0.3 — не найдено нигде для меню (`MenuParts::Item(label, shortcut, selected, enabled)`, `src/app/ui/design/components/MenuParts.h:63`, имеет `enabled`, но не alpha; `ImGuiStyleVar_Alpha` как приём в коде используется — `StageNodeBodies.cpp:~220`).

### Выводы для реализации (тема A)
- Файлы: `src/app/frame/StagePopupsB.cpp` (рендер меню: затемнение вне `gLinkDragSuggestions` через `ImGui::BeginDisabled`/`PushStyleVar(Alpha, 0.3)` вокруг `MenuParts::Item`, плюс в ветке поиска и в подменю категорий; сделать множество имён `unordered_set` вместо линейного вектора — иначе O(N²)); `src/app/graph/Wiring.cpp` (при желании param-пины в `RecommendedNodeTypesForOutput`); `src/app/AppState.cpp/AppShared.h` (при выносе в set).
- Объём: **S** для затемнения/фильтра по уже имеющемуся списку; **M** если добавлять param/color-пины и общий `PortKind`-дескриптор; **L** при полном рефакторе с реестром дескрипторов без проб.
- Риски: (1) 347 проб `MakeNode` на drop (задержка/побочные эффекты конструкторов — не проверено); (2) 8 дублирований вычисления «src-фактов» — расхождение правил; (3) динамические порты и ноды, у которых дефолтный экземпляр без нужного слота; (4) `IsUserSpawnable`-ноды (расширения/плагины, `Extensions.cpp`) регистрируются позже старта — кэш нужно строить лениво; (5) UX: затемнённые пункты по-прежнему кликабельны — решить, блокировать ли (автосоединение для них не сработает).

## Тема B. Баг: подсказка параметра «съедает» часть интерфейса ноды

### Как рисуются подсказки
- Механизм — обычные ImGui tooltip-окна (`ImGui::BeginTooltip/SetTooltip`), собственных draw list/слоёв канваса нет. Единая обёртка `HelpTip(fmt,…)` в `src/app/ui/ParamWidgets.cpp:41-82`: включается опцией `CategoryColors::GetTooltips()`, задержка `tok::motion_tooltip_delay`, ширина 24 em.
- Внутри ноды (`gInsideNodeCanvas == true`, ставится между `ed::BeginNode` и `ed::EndNode`: `src/app/frame/StageNodeBodies.cpp:217`/`:1507`) `HelpTip` **не рисует**, а паркует текст в `gNodeHelpTip` (`ParamWidgets.cpp:66-72`); рисуется после цикла нод в блоке `ed::Suspend()` (`src/app/frame/StageKeyboard.cpp:1010-1037`), т.е. в экранных координатах, поверх всего (tooltip-окна ImGui всегда сверху). Зум учитывается тем, что рисуем вне канвас-трансформа (тот же приём — `StageLinks.cpp:609-612`, `AppState.cpp:1192-1205`: в v2 подсказка «плыла» по зуму/пану). Clip rect/scissor/очистки фона под подсказкой нет.
- Это уже исправление: в коде прямо сказано, что `ed::Suspend()` посреди ноды «портит draw state — строки над подсказкой исчезают» (`ParamWidgets.cpp:63-65`, `AppState.cpp:1282-1284`) — это и есть описанный баг.

### Гипотезы (по убыванию уверенности)
1. **(~65%) Подсказки, вызываемые напрямую из тела ноды, минуя отложенную очередь.** Без `HelpTip` и без паркинга текста рисуются `ImGui::SetTooltip/BeginTooltip` внутри `BeginNode…EndNode`, т.е. при активном `ImGuiEx::Canvas` (канвас-трансформ + разделённые draw-каналы). Места: `ParamWidgets.cpp:565,567` (`EndParamDraw`: «Off: set by…» / «Off in this mode» — это как раз подсказка **параметра** отключённой/зависимой ручки), `src/app/ui/design/components/NodeHeader.h:69` (иконка issue), `FieldWell.h:219`, `FormParts.h:155`, `CodeBlock.h:50`, `FilterBar.h:117`, `StageNodeBodies.cpp:1258`, `bodies/AudioNodeBodies.cpp:322`, `bodies/ParamBodies2.cpp:442`, `bodies/GeometryParams.cpp:2428`. Всего в проекте 75 прямых вызовов tooltip против 95 `HelpTip`. Симптом совпадает с описанным в комментариях: строки ниже/выше пропадают, подсказка смещается по зуму. Особенно подозрителен `EndParamDraw` (`ParamWidgets.cpp:552-568`): он ещё и **правит вершины** draw list ноды (`FadeInactive(dl, sParamVtxStart)`, `:528,:561`) прямо перед подсказкой — если `dl` не «свой», порча чужих вершин даёт исчезающий UI. Проверить: поведение при наведении на отключённый параметр vs на обычный.
2. **(~25%) Приоритет/перезапись в отложенном слоте.** После цикла нод `gNodeHoverTip` (cook-warning/issue) перекрывает `gNodeHelpTip` (`StageKeyboard.cpp:1028-1037`: `if (!gNodeHoverTip.empty()) … else if (!gNodeHelpTip.empty()`); а `gGlobalScaleTooltipHovered` рисуется отдельным `BeginTooltip` до них — два tooltip-окна в один кадр перекрывают друг друга, второй затирает первый. Это даёт «пропала подсказка/часть текста», но не «пропала часть интерфейса ноды».
3. **(~10%) `ImGuiStyleVar_Alpha` 0.55 для «dimmed» нод** (`StageNodeBodies.cpp:~220`): прямой tooltip внутри ноды наследует alpha, и фон подсказки становится полупрозрачным — интерфейс под ней «просвечивает»/выглядит съеденным. Для отложенных подсказок не актуально (Pop идёт до ed::EndNode).
- Не проверено: воспроизведение в запущенном приложении (сборки нет, запрещено); поведение `imgui_canvas.cpp` при `BeginTooltip` внутри канваса — смотрел только комментарии (`imgui_canvas.cpp:169,272,292,439`: предупреждения о несовпадении Suspend/Resume и пустых draw-командах).

### Минимальное исправление (словами)
Завести единый путь для всех подсказок внутри ноды: каждый прямой `SetTooltip/BeginTooltip`, выполняемый при `gInsideNodeCanvas == true`, заменить на вызов существующего `HelpTip` (он уже умеет паркинг в `gNodeHelpTip` и рисует после цикла нод). В первую очередь — `ParamWidgets.cpp:565-567`, затем остальные из списка. Альтернатива — перехват в одной точке: тонкая обёртка `NodeTip()` с проверкой `gInsideNodeCanvas`, а прямые вызовы запретить правилом/greps в CI. Попутно разрешить конфликт приоритетов: показывать одно окно подсказки за кадр.

### Выводы для реализации (тема B)
- Файлы: `src/app/ui/ParamWidgets.cpp` (565/567), `src/app/ui/design/components/{NodeHeader,FieldWell,FormParts,CodeBlock,FilterBar}.h`, `src/app/frame/StageNodeBodies.cpp:1258`, `src/app/bodies/{AudioNodeBodies,ParamBodies2,GeometryParams}.cpp`, `src/app/frame/StageKeyboard.cpp` (приоритет).
- Объём: **S** (около 10 точечных замен), если гипотеза 1 верна; **M**, если придётся разбираться с `FadeInactive`.
- Риски: подсказки в `design/components/*` используются и вне нод (панели) — обёртка должна работать в обоих контекстах (проверять `gInsideNodeCanvas`); дедуп нескольких подсказок за кадр; опция `GetTooltips()` отключает «help», но диагностические подсказки намеренно показываются всегда (`ParamWidgets.cpp:39-40`) — не сломать это различие; воспроизведение пока не подтверждено (**не проверено**).
