# Панели, выделение, настройки, i18n, патчи, сборка — разведка для инспектора

Только чтение кода; ничего не собиралось и не запускалось. Пути — от корня репозитория. Всё, что прямо не сказано «проверено», выведено из чтения исходников.
Стек UI: Dear ImGui + imgui-node-editor (`external/imgui`, `external/imgui-node-editor`, namespace `ed::`). Почти весь UI-код — в `namespace app`.

## 1. Система панелей

**Базового класса/интерфейса панели нет.** Это сознательно: `docs/CODE_STANDARDS.md` §1 («No premature abstraction… не превращай в фреймворк базовых классов»). Панель = набор глобальных переменных + свободные функции, а «докинг» — собственная раскладка в одном окне ImGui (не ImGui docking-ветка).

Состояние панели (на примере Mod Matrix, `src/app/AppShared.h:775-799`, определения `src/app/AppState.cpp:447-505`):
- `gModMatrixOpen` (bool), `gModMatrixDock` (int), `gModMatrixWidth/Height` (float), константы `kModMatrixMinWidth=300 / MinHeight=140`.
- Коды дока: 0 = Bottom, 1 = Right, 2 = Left, 3 = Top, `kDockFloating = 4` (`AppShared.h:764`). Порядок подтверждён `ModMatrixDockCombo` (`src/app/panels/ViewportPanel.cpp:622`).
- Часть новых панелей ограничена: Curve Editor — только bottom/top (`src/app/panels/CurveEditorPanel.h`), у новых панелей (3D View, Piano Roll, Studio) заголовки/глобалы объявлены прямо в `AppShared.h` или в собственном `.h`.

Отрисовка (три точки входа у каждой панели):
1. `DrawModMatrixDocked(id, size)` — `src/app/panels/ModMatrixPanel.cpp:999`: внешний `BeginChild(id,size)`, полоса-«грип» (`InvisibleButton`) на стороне холста — тянется мышью, меняет `gModMatrixWidth/Height` по `dock` (switch, строки ~1020-1035) с клампом по `kMin*`; карточка контента `PanelFrame::BeginCard` (`app/ui/design/components/PanelFrame.h`) с `PushDockedPanelStyle`; содержимое — `DrawModMatrixTable()`. Тонкость: перед `EndChild` вручную обнуляется `ItemSpacing` (иначе полоса windowBg между панелями) — копировать как есть.
2. `DrawModMatrixFloating()` (`ModMatrixPanel.cpp:1105`): `BeginFloatingPanel(title###id, &open, firstSize)` → тот же контент → `EndFloatingPanel()`; закрытие крестиком сбрасывает `gModMatrixOpen`. Хелпер — `ViewportPanel.cpp:635`.
3. Размещение в кадре — `src/app/frame/StageLayout.cpp` и `StageSidePanels.cpp` (куски бывшего `main()`, общие локали через `FrameCtx.h`).

Раскладка кадра (важно для инспектора):
- `StageLayout.cpp:101-104` — флаги `matrixBottom/Right/Left/Top = Open && Dock==N`.
- `StageLayout.cpp:279-283` — бюджет высоты top/bottom (`topBottom += gModMatrixHeight`), «каждая top/bottom-панель — своя строка»; недоучёт ломает скроллбар окна-оболочки.
- `StageLayout.cpp:322` / `:346` — top- и left-панели рисуются ДО холста (`SameLine(0,0)` для left).
- `StageSidePanels.cpp:~660-675` — `rightReserved += gModMatrixWidth` (общий резерв правых панелей → `graphWidth` холста, `ed::Begin("graph", ImVec2(graphWidth, graphHeight))` в `StageLayout.cpp:440`); `:387` — рисование right-панели после `ed::End()` через `SameLine(0,0)`; `:707` — bottom.
- Клэмпы максимальной ширины/высоты соседей считаются там же (`StageLayout.cpp:~240`, формулы `maxArrangeWidth` вычитают ширины других панелей) — новую панель надо вписать и туда.
- Floating: `src/app/frame/StageFloating.cpp:40-41`.

Рейка и хоткеи:
- Рейка: `DrawPanelRail()` в `src/app/frame/StageMenuBar.cpp:25-` — `PanelRail::Item("##railModMatrix", y, IconsInfinite::GridDots, nullptr, gModMatrixOpen, T("Modulation matrix"))`, ряды по `top + N*step`, нижние кнопки (Settings, History, Terminal) от `bottom`. Иконки — `IconsInfinite::*` (`src/app/ui/design/Glyphs.gen.h`, генерируемый).
- Меню View: `StageMenuBar.cpp:~341` — `MenuParts::Check(L("Show modulation matrix"), &gModMatrixOpen)`, затем выбор дока (`ModMatrixDockCombo`) и слайдеры ширины/высоты.
- Хоткей: `src/app/frame/StageKeyboard.cpp:166-169` — `if (!typing && shiftOnly && IsKeyPressed(ImGuiKey_M,false)) gModMatrixOpen = !gModMatrixOpen;`. Занято: Shift+ V M P T S U W R 3 E (+ N/H/K канвасные по комментарию `:68`). Справочное окно хоткеев — `DrawShortcutsWindow` (не проверено, где именно перечислены строки). Свободные кандидаты — по `grep ImGuiKey_` в `StageKeyboard.cpp` (не проверено полностью); `Shift+I` для Inspector выглядит свободным — не проверено.

**Где хранится раскладка: в настройках приложения, НЕ в патче.** `SaveWorkspaceSettings()` / `LoadWorkspaceSettings()` (`src/app/graph/PatchIO.cpp:436 / :318`) пишут плоский `key=value` файл `<SettingsDir>/Infinite.workspace-settings` (`WorkspaceSettingsPath`, `:311`): `panelMatrixOpen/Dock/W/H` (`:461-464`, чтение `:371-379` с `clamp(dock,0,kDockFloating)`). Автосохранение при изменении — `PersistPanelLayoutIfChanged()` (`:495`): строит строку-слепок из флагов/доков/размеров и пишет через 1 с после стабилизации (в тест-запусках `INFINITE_EXITAFTER`/`INFINITE_OPENPANELS` пропускается). **Новую панель нужно добавить в слепок `snprintf` (`:502`), иначе изменения не сохранятся сами.** Исключение — `gViewportPanelNodes` (список карточек вьюпорта) — лежит В ПАТЧЕ (`Patch.h:539`, `PatchIO.cpp:223`, чистится `:1071`).

### Чек-лист «добавить новую панель» (эталон Mod Matrix)
1. Глобалы: `gXOpen/gXDock/gXWidth/gXHeight` + `kXMinWidth/Height` — `extern` в `AppShared.h` (или в собственном `app/panels/XPanel.h`, как Curve Editor — компилируется быстрее, комментарий в хедере), определения в `AppState.cpp`.
2. Файл `src/app/panels/XPanel.cpp`: `DrawXDocked`, `DrawXFloating`, `XDockCombo`, содержимое `DrawXContent` — копия каркаса `DrawModMatrixDocked` (грип + PanelFrame + обнуление ItemSpacing). Добавить файл в явный список `set(COMMON_SOURCES …)` (`CMakeLists.txt:453`; glob там только для ресурсов).
3. `StageLayout.cpp`: флаги по доку, строка в бюджет `topBottom`, вызовы Top/Left; `StageSidePanels.cpp`: резерв `rightReserved`, вызовы Right/Bottom; `StageFloating.cpp`: floating; клэмпы соседей.
4. Рейка (`StageMenuBar.cpp` `DrawPanelRail`), меню View, хоткей (`StageKeyboard.cpp`), иконка в `IconsInfinite`.
5. Сохранение: ключи в `SaveWorkspaceSettings`/`LoadWorkspaceSettings` и слепок в `PersistPanelLayoutIfChanged`.
6. Строки через `T()/L()/I18N_KEY` + 5 TSV (см. §4).
7. Тесты: у панелей есть фикстуры `INFINITE_MODMATRIXTEST/GEOM` (`StageTestsB.cpp:309`, `StageFloating.cpp:718`, `Startup.cpp:4325`), в `INFINITE_OPENPANELS=…` (`Startup.cpp:4389`) перечисляются панели для скриншотов; скилл `.claude/skills/panels-sweep` (driver.sh). Добавить хотя бы открытие панели в `OPENPANELS`.

## 2. Выделение нод

- Собственного хранилища selection **нет**: это состояние imgui-node-editor. Контекст — `ed::EditorContext* gEditor` (`AppState.cpp:70`, `AppShared.h:269`). Нода ↔ id: `GraphNode::NodeId() = index * kStride` (kStride=1050; `src/core/GraphNode.h:36,117`), обратно `FindNodeByIndex(id / GraphNode::kStride)`; стабильный идентификатор — `gn.uid` (uint64, `Patch.h` NodeRecord.uid), `FindNodeByUid`.
- Мульти-выделение есть: `ed::GetSelectedObjectCount()`, `ed::GetSelectedNodes(buf,n)`, `ed::GetSelectedLinks`, `ed::IsNodeSelected(id)`. Примеры: `StageKeyboard.cpp:82-87`, `:263`, `:378`; RPC `graph/RpcApp.cpp:388`, `RpcTerminal.cpp:61`.
- Программно выделить: `ed::SelectNode(id, append)`; отложенно через `gPendingSelect` (`AppShared.h:1895`, применяется в начале кадра внутри `ed::Begin`, `StageLayout.cpp:453-461`).
- **События/колбэка «выделение изменилось» нет.** Паттерн — опрос каждый кадр:
  - `View3DPanel.cpp:~285-310` `FollowCanvasSelection()` — запоминает `sLastNode`, реагирует только на смену; нужен `ed::SetCurrentEditor(gEditor)` (панель рисуется вне редактора), после — восстановить предыдущий.
  - `PianoRollPanel.cpp:~500-525` — то же для PianoRollNode; `CurveEditorPanel.cpp:276,348` — для MSEGNode.
  - `Minimap.cpp:80` (подсветка), `NoteBodies.cpp:698`.
- Для инспектора: взять эти циклы как образец — завести `Inspector::SelectedNode()` с опросом (`GetSelectedObjectCount()==1 && node`), либо хранить `uid` с обработкой удаления ноды (карточки вьюпорта чистятся при удалении, `panels-sweep`).
- ВАЖНО: `ed::*` вызываются из-под `ed::SetCurrentEditor(gEditor)`; правые/нижние панели рисуются после `ed::End()`, левые/верхние — до `ed::Begin` (данные об выделении предыдущего кадра; один кадр задержки допустим).

## 3. Настройки приложения

Нет единого класса настроек — несколько плоских файлов в `AppPaths::SettingsDir()` (`src/platform/AppPaths`), пишутся функциями в `src/app/graph/PatchIO.cpp`:
- `Infinite.workspace-settings` — `key=value`, расширяемый (`LoadWorkspaceSettings :318`, `SaveWorkspaceSettings :436`): snapToGrid, showCanvasGrid, minimap*, cableVisibilityMask, **panel\*** (раскладка), и т.п. Неизвестные ключи игнорируются → безопасно для обратной совместимости.
- `Infinite.autosave-settings` — ПОЗИЦИОННЫЙ файл (строка = поле; `LoadGeneralSettings :269`). Новые поля — только в конец; для нового булева лучше НЕ сюда.
- `Infinite.audio-settings` (`:524-`), `ExprGlobals` (`:636`), темы (`CategoryColors`), recents (`Patch.cpp`).
- Старт: `LoadWorkspaceSettings()` из `Startup.cpp:1618`.

Добавить булев параметр (напр. `gInspectorFollowsSelection`):
1. `extern bool …;` в `AppShared.h`, определение с дефолтом в `AppState.cpp` (образец `gShowCanvasGrid`, `AppState.cpp:198`).
2. Ветка `else if (key == "inspectorFollow") x = (val != "0");` в `LoadWorkspaceSettings` и `file << "inspectorFollow=" << (x?1:0) << "\n";` в `SaveWorkspaceSettings`.
3. Окно Settings: `src/app/panels/SettingsWindow.cpp`; вкладки `kTabs` (`:94`: Appearance, Canvas & Workspace, Audio, General & Performance, Field Language, Expression Globals, Extensions, Plugins, Agents & Terminal, Infinite as VST3/AU). Вкладка «Canvas & Workspace» (`sTab == 1`, `:450-`): `if (FormParts::Switch(L("Show canvas grid"), &gShowCanvasGrid)) { SaveWorkspaceSettings(); }` — образец. Заголовки секций `FormParts::Section(T("…"))`.

## 4. Локализация

- 6 языков: en (ключ-источник), es, de, zh, ja, ru. Реестр `kLanguages` в `src/core/I18n.cpp:~141` (`I18n::Languages()`). Все `.tsv` — в `resources/lang/` (`de/es/ja/ru/zh.tsv` по 2935 строк + `glossary.tsv`). Копируются CMake’ом (`CMakeLists.txt:~952`).
- Формат `ru.tsv`: `English-ключ<TAB>перевод`, `#`-комментарии; `=en` — намеренно как в английском; экранирования `\n \t \\ \c`. Русский помечен beta (машинный).
- API (`src/core/I18n.h`): `T("текст")` — обычный текст/тултипы; `L("Label")` — метки виджетов (ID-часть `##/###` не переводится); `TC/LC(context,key)`; `I18N_KEY("…")` — пометить литерал в массивах, переводимых при выводе (`T(arr[i])`); `TList` для `Combo`. Недостающий перевод → английский. Нельзя вызывать `setlocale` (ломает парсинг патчей).
- Добавить строку: написать `T("Inspector")`/`L("…")` в коде → добавить строку в КАЖДЫЙ из 5 `.tsv` (de/es/ja/ru/zh; ключи отсортированы по алфавиту — вставлять по месту). Инструменты: `tools/i18n/extract.py keys|report|stub LANG` (`report` падает с кодом 1 при пропусках/несовпадении printf), `tools/i18n/lint.py` ловит «голые» литералы в `src/app/` (исключены `src/app/bodies/`, `selftest/`, `ParamWidgets.cpp`; пометка `// i18n-ok`). Следствие: имена параметров нод и значения выпадающих списков в `bodies/` — **английские по дизайну (D2)**, переводить их в инспекторе не нужно.
- ВАЖНО для CJK: глифы подмешиваются по таблице активного языка (`GlyphsForCurrentLanguage`), новые строки без шрифтового сабсета ломаются — `tools/i18n/subset_fonts.py` (не проверено, нужен ли перезапуск при добавлении строк).

## 5. Сохранение патча

- Формат: **построчный текст**, не JSON (`src/core/Patch.h:1-100` — авторитетная схема; `Patch.cpp`). Заголовок `infinite-patch 1` (`kMagic`, `kVersion=1`, `Patch.cpp:22-23`). Есть также `src/core/PatchJson.cpp` (JSON-вид для CLI/RPC — не проверено подробно) и бинарный/текстовый снапшоты для undo и буфера (`WriteText`, «two serializers», см. panels-sweep).
- Блок ноды: `node <index> <category> <type>` / `pos <x> <y>` / `flags <showParams> <bypassed> <showMiniViewport> <showAdvancedParams>` / `f|i|b|c|s <name> <value>` / `end` (`Patch.cpp:298` пишет `pos`, `:736` читает). `NodeRecord` — `Patch.h:139`: `index`, `uid`, `x,y`, `showParams` («раскладка параметров раскрыта», `GraphNode.h:51`), `bypassed`, `showMiniViewport`, `showAdvancedParams`, `params`. Размеров ноды отдельно нет — «свёрнутость» = `showParams`.
- Обратная совместимость: (1) неизвестный ключ/тип ноды пропускается с предупреждением; (2) файл новее `kVersion` отвергается («written by a newer version»); (3) новые поля добавляются **в конец строки** как опциональные токены (пример — `mod … [lo hi [enabled]]`, комментарии в `Patch.h`); (4) `rev`-строка и `kRevision=2` (`Patch.h:137`): «ключ отсутствует = старое поведение» только для узлов, сохранённых старыми билдами (`SavedBefore`); (5) отдельные строки вместо новых полей, чтобы старые билды читали остальное (`streamid`). Решение владельца (`docs/DECISIONS.md`, 2026-10-13): повторное открытие патча не обязано быть идентичным, патч обязан открыться/играть/показать картинку.
- Для инспектора новое UI-состояние в патч, скорее всего, не нужно (раскладка панели → workspace-settings). Если понадобится хранить «закреплённую ноду» — отдельная строка + `Patch::Data` поле + `BuildPatchData`/`ApplyPatchData` (`PatchIO.cpp:23`, `PatchLoad.cpp`) + прогон `MODMATRIXTEST`-подобного round-trip.

## 6. Сборка под macOS

- Система: **CMake ≥ 3.16** (`CMakeLists.txt:1`, `CMAKE_CXX_STANDARD 17` на строке 38; README для Linux пишет C++20 — расхождение, не проверено). Один исполняемый `Infinite` (`add_executable(Infinite ${COMMON_SOURCES})`, `:1234`) + ~10 вспомогательных check-таргетов (`field-preset-check`, `sketch-check`, `slice-check`, `pianoroll-check`, `terminal-check`, … `:1076-1169`). `ctest`/`add_test` в CMake **нет**.
- Команда (README «Build from source» → macOS): `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8 && open build/Infinite.app`. Требуется Xcode CLT. Время в README/доках не указано (не проверено). Зависимости — вендорены в `external/` (imgui, node-editor, glad, miniaudio, quickjs-ng, lunasvg, …) и `third_party/`; системные — только фреймворки macOS.
- Подмодуль один: `external/vst3sdk` (`.gitmodules`), каталог пуст. `INFINITE_ENABLE_VST3` по умолчанию **ON** (`CMakeLists.txt:389`); без SDK configure падает с подсказкой `git submodule update --init external/vst3sdk` + `git -C external/vst3sdk submodule update --init pluginterfaces public.sdk` (`:412-437`; в git-worktree CMake пытается взять SDK из основного чекаута). Для сборки без SDK — `-DINFINITE_ENABLE_VST3=OFF` (не проверено, что остальное собирается без него).
- Дисциплина сборки (`.claude/skills/build-discipline`): никаких `make -jN` руками; `scripts/ibuild.py status|plan|build [--mode dev]|obj <file>`; режим dev = Debug+arm64 в `build-dev/`, `-j` ≤ 4; машина владельца — 8 GB, «один билд на Mac»; ccache; семи-сборка `ibuild obj file.cpp` для проверки синтаксиса. Широкие заголовки (`AppShared.h` 6581 строк!) — правка ведёт к пересборке сотен файлов: предпочитать правки в `.cpp` и собственные `.h` панели.
- Тесты: нет unit-фреймворка; self-test через env-переменные: `INFINITE_<NAME>TEST=1 INFINITE_EXITAFTER=10 ./build/Infinite.app/Contents/MacOS/Infinite` печатает pass/FAIL. Драйверы: `.claude/skills/run-infinite-hygiene/driver.sh [--skip-build] [--fast|--group <area>|--full]` (~50 проверок), `.claude/skills/panels-sweep/driver.sh`, `.claude/skills/node-ui-sweep/driver.sh`. Каталог `tests/` (chat, field, headless, platform, plugin, render, render3d, ui-golden, website) — часть через эти драйверы. Pre-push-хук запускает `--group ui` (локальный `.git/hooks`, у владельца; в клоне не проверено).

## 7. Контрибуция и агентские инструкции

- `CONTRIBUTING.md` (26 строк): маленькие сфокусированные PR (пачки несвязанных идей могут быть «переписаны нативно» с кредитом автору); MIT, **clean-room** — не копировать/не опираться на GPL/AGPL; следовать `docs/CODE_STANDARDS.md`; каждая `Platform::`-функция — с macOS/Windows/Linux сторонами (`docs/WINDOWS_COMPATIBILITY_STANDARDS.md`); сборка/тесты — в README.
- `docs/CODE_STANDARDS.md`: без преждевременных абстракций; повторять идиому файла; комментарии — только «почему»; валидация только на границах; тест на каждый новый DSP/стейтфул; «UI-изменения нужно запускать, а не только компилировать»; новые файлы в существующую структуру `src/<category>/`; ARCHITECTURE.md — источник «куда класть» (редактор UI = категория 3).
- `AGENTS.md` (98 строк; `AGENT.md` 236 и `.cursorrules` 89 — про написание патчей/Field для внешних агентов, к правке кода не относятся): начинать с `docs/DECISIONS.md` и `infinite-lifecycle`; «**перед действием обязательно прочесть релевантный skill**» из `.claude/skills/` (симлинк `.agents/skills/`). Для нашей задачи релевантны: `node-ui-pillars` (симметрия/контраст, грузить до любой правки UI ноды), `infinite-design-system` (токены, компоненты, моушн, CI-ratchet — до любых правок стиля/хрома), `panels-sweep`, `shortcuts-sweep`, `codebase-navigation`, `bug-blast-radius`, `build-discipline`, `windows-parity`/`linux-parity`, `infinite-code-review`, `git-branch-workflow`, `ci-preflight`. Агенты `.claude/agents/`: triage, infinite-planner, cartographer, prior-art-scout, verify-gate.
- Процессные правила `AGENTS.md`: «two laptops» — Primary (`Namans-MacBook-Pro-366`) мержит `main` и пушит, Secondary (Air) работает в ветках `air/<slug>` и только коммитит (`docs/plans/two-laptops/README.md`; применимость к форку пользователя — не проверено); три платформы; бренд-правила только для маркетинговых поверхностей; UI приложения — по `infinite-design-system`.
- Инварианты: clean-room (не открывать GPL-исходники; исключение для Blender только для 3D View); синтаксис Field без сигилов.

## Выводы для реализации инспектора

**Куда встраивать.** Как ещё одну докаемую панель по образцу Mod Matrix (Right по умолчанию, как у TouchDesigner; поддержать Left/Floating почти бесплатно копированием каркаса). Содержимое — `DrawInspectorContent()`: читает выделенную ноду опросом `ed::` (паттерн `FollowCanvasSelection`) и выводит её параметры. Сами виджеты параметров рисуются функциями из `src/app/bodies/*` и `ParamWidgets.cpp` (`ModSlider`, `BeginNodeParams`), завязанными на контекст ноды/`ed::` и регистрацию в `Modulation::FrameParams` — как переиспользовать их вне тела ноды, смотрит параллельная сессия (`docs/research/node-params-ui.md`), здесь не дублируется.

**Минимальный набор файлов.** (1) новый `src/app/panels/InspectorPanel.cpp` + `InspectorPanel.h` (глобалы в собственном `.h`, не в `AppShared.h`); (2) `src/app/AppState.cpp` — определения (или в самом `.cpp` панели); (3) `StageLayout.cpp` + `StageSidePanels.cpp` + `StageFloating.cpp` — бюджет/резерв/вызовы; (4) `StageMenuBar.cpp` — рейка и пункт View; (5) `StageKeyboard.cpp` — хоткей; (6) `PatchIO.cpp` — ключи workspace и слепок `PersistPanelLayoutIfChanged`; (7) `Glyphs.gen.h`/`IconsInfinite` — иконка (или переиспользовать существующую); (8) `resources/lang/{de,es,ja,ru,zh}.tsv`; (9) `CMakeLists.txt:453` (явный `COMMON_SOURCES`); (10) булев «показывать параметры только в инспекторе» — `AppShared.h`/`AppState.cpp`/`SettingsWindow.cpp`. Патч-формат менять не требуется, если режим «компактная нода» — глобальная настройка; если per-node — новое поле `flags`-хвоста (см. §5).

**Главные риски.**
1. `AppShared.h` (6.5k строк) включается повсюду — правки в нём = долгая пересборка на 8-ГБ машине; держать код в своём `.h/.cpp`.
2. Раскладка кадра хрупкая: учёт `topBottom`/`rightReserved`/клэмпов соседей и обнуление `ItemSpacing` вокруг `EndChild` — типовые баги скроллбара и «полос» (см. длинные комментарии в `StageSidePanels.cpp`, `ModMatrixPanel.cpp`); нужен прогон `panels-sweep` для 4 доков.
3. Параметры ноды рисуются вызовом тела ноды внутри `ed::` и одновременно регистрируются в модуляции (`ParamRef.paramIndex` = порядок отрисовки, `GraphNode.h`, `PatchSchema.h`); отрисовка тех же параметров в другой панели может задвоить регистрацию/ID виджетов (`##` ID, `gCurrentNodeIndex`), сломать привязки модуляции и ModMatrix (`MODMATRIXTEST`: свёрнутая нода всё равно должна регистрировать параметры).
4. Selection — только `ed::`-опрос; левая/верхняя панель видит выделение с задержкой в кадр; мульти-выделение и удалённая нода (устаревший `uid`/index — `RemoveNodeByIndex` переиспользует index) требуют аккуратной обработки.
5. Дисциплина репозитория: чтение skill перед правкой (`node-ui-pillars`, `infinite-design-system` с CI-ratchet на токены/иконки), i18n-lint для `src/app/`, Windows/Linux-паритет (в `src/app` в основном платформо-нейтрально, но проверить), хоткей-коллизии (`shortcuts-sweep`).
6. Сборка: `external/vst3sdk` пуст → configure упадёт без submodule init или `-DINFINITE_ENABLE_VST3=OFF`; время холодной сборки не документировано (не проверено), на dev-режиме `ibuild` ожидается долго.
7. Не проверено: применимость pre-push-хука/«two laptops» к этому клону; необходимость регенерации font-сабсетов при новых строках; свободность хоткея.
