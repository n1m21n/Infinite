# Реестр изменений форка в коде апстрима

| Файл | Что изменено | Зачем | Ветка |
|------|--------------|-------|-------|
| `src/app/panels/InspectorPanel.{h,cpp}` (новые) | Панель-инспектор, настройка, обёртки пинов `insp::*`, self-test `INFINITE_INSPECTORTEST` | Этап 3 `docs/plans/ux-roadmap.md` | `my/inspector` |
| `src/app/frame/StageNodeBodies.cpp` | Диспетчер `dynamic_cast → Draw*Params` вынесен в `DrawNodeParamsBody(GraphNode&)` (тело без изменений); вызов `InspectorFollowSelection()`; `InspectorOwnsParams(gn)` в условии register-only прохода | Один диспетчер на канвас и инспектор | `my/inspector` |
| `src/app/ui/ModWidgets.cpp`, `ui/ParamWidgets.cpp`, `ui/Theme.cpp`, `bodies/GeometryParams.cpp`, `bodies/SamplerBodies.cpp` | `ed::BeginPin/EndPin/PinPivotAlignment/PinRect` → `insp::*` (механическая замена) + include | Вне канваса `ed::BeginPin` нельзя: в инспекторе точка рисуется обычным ImGui | `my/inspector` |
| `src/core/Modulation.h`, `src/core/Palette.h` | `TruncateFrameParams(n)` / `TruncateFrameColors(n)` | Снять дубли регистраций полного прохода инспектора | `my/inspector` |
| `frame/StageLayout.cpp`, `StageSidePanels.cpp`, `StageFloating.cpp` | Док Left / Right / Floating инспектора | Раскладка | `my/inspector` |
| `frame/StageKeyboard.cpp`, `StageMenuBar.cpp` | Shift+I, пункт View → Show inspector, кнопка на рейке | Открытие панели | `my/inspector` |
| `frame/StageMenuBar.cpp` (верхняя панель) | Вызов `DrawTopBarParamsTools(centreY)` после кнопки аудио: плитки «показать все параметры», «скрыть все параметры» (подсветка = все открыты/все скрыты, в подсказке счётчик открытых) и переключатель инспектора; пункты меню Nodes остались | Быстрый доступ | `my/inspector` |
| `graph/PatchIO.cpp`, `panels/SettingsWindow.cpp` | Ключи `inspector*` в workspace-settings; строка «Node parameters» в Canvas & Workspace | Сохранение раскладки и настройки | `my/inspector` |
| `frame/StageTestsB.cpp`, `CMakeLists.txt` | Вызов self-test; новый .cpp в списке | Тест/сборка | `my/inspector` |
| `src/core/CategoryColors.{h,cpp}` | `Presets()` → изменяемый список (резерв 200 мест), поля `cables/user/file/base` в `Preset`, API `SetUserPresets/IsUserPreset/FindPreset/...`, кабели темы в `DefaultCableColor`, `PresetNames` пересобирается | JSON-темы (этап 4а) | my/themes |
| `tools/design/build_tokens.py`, `Tokens.gen.h` | цветовые роли `inline` + дефолты + реестр `kColorTokens`; генерация `docs/theme-tokens.md` | переопределение токенов (4б) | my/themes |
| `src/app/Startup.cpp`, `frame/StageFramePump.cpp`, `panels/SettingsWindow.cpp`, `CMakeLists.txt` | крючки `// fork:` (Init до LoadPreference, Tick перед NewFrame, кнопки/кружки в Appearance, +ThemeFiles.cpp) | JSON-темы | my/themes |
| новое: `src/app/ui/ThemeFiles.{h,cpp}`, `docs/themes.md`, `docs/theme-tokens.md` | загрузчик/горячая перезагрузка/экспорт | JSON-темы | my/themes |
| `src/core/OscMessage.{h,cpp}` | `DecodePacket` (bundle, рекурсивно), `MatchAddress` (`*`,`?`), теги `T/F/d/h` в `DecodeMany`; `Decode` теперь через `DecodeMany` | Bitwig/DrivenByMoss шлёт bundle'ами | my/osc |
| `src/nodes/OscNodes.{h,cpp}` | OSC Receive переведён на общий приёмник порта; параметр `argIndex`, wildcard, OSC learn, `BindError()` | несколько нод на порт, learn, индикация ошибки | my/osc |
| `src/nodes/OscPortHub.{h,cpp}` (новые) + `CMakeLists.txt` (+1 строка) | один сокет/поток на порт, буфер 64 КБ, раздача по подписчикам | | my/osc |
| `src/app/bodies/ParamBodies1.cpp` | `DrawOscReceiveParams`: arg index, кнопка OSC Learn, строка ошибки, диапазон low/high 0..127 | UI | my/osc |
| `src/app/selftest/MiscTests.cpp` | в `RunOscControlTest` добавлены проверки OSC Receive (bundle, общий порт, wildcard, learn, bind-ошибка) + includes | self-test | my/osc |
| `src/app/ui/NodeTip.h` (новый), `ParamWidgets.cpp` | `app::NodeTip()` — обёртка диагностических подсказок: внутри ноды (`gInsideNodeCanvas`) паркует текст в `gNodeHoverTip`, вне — рисует сразу | прямые `SetTooltip/BeginTooltip` внутри `BeginNode…EndNode` ломали отрисовку ноды | my/quick-fixes |
| `ParamWidgets.cpp:EndParamDraw`, `design/components/{CodeBlock,FormParts,FieldWell,FilterBar,NodeHeader}.h`, `bodies/{ParamBodies2,AudioNodeBodies,GeometryParams}.cpp`, `frame/StageNodeBodies.cpp` | `ImGui::SetTooltip/BeginTooltip` → `app::NodeTip` (10 мест, исполняются внутри нод) | то же | my/quick-fixes |
| `frame/StageKeyboard.cpp` (~1015) | одна подсказка за кадр: scale-toggle → `gNodeHoverTip` → `gNodeHelpTip` (else-if) | два tooltip-окна в кадре перекрывали друг друга | my/quick-fixes |
| `frame/StagePopupsB.cpp` (меню "search") | при протягивании кабеля несовместимые ноды рисуются с alpha 0.3 (подменю категорий и текстовый поиск; в поиске совместимые первыми), остаются кликабельными; для кабеля от модулятора не применяется; множество совместимых имён — `unordered_set` (файловый static, пересобирается при смене drag-а); `AppShared.h` не тронут | этап 2 ux-roadmap | my/quick-fixes |
| `src/app/graph/PortProbeCache.{h,cpp}` (новые), `CMakeLists.txt` (+1 строка), `graph/Wiring.cpp:RecommendedNodeTypesForOutput`, `frame/StageLinks.cpp:RecommendedNodeTypesForPin`, `frame/StagePopupsB.cpp` (прогрев) | кэш портов по имени типа ноды: подсказки на drop кабеля считаются из кэша (маска принимаемых видов источника + виды выходов), без `MakeNode` на каждый drop; прогрев 3 мс/кадр, типы из расширений достраиваются при первом использовании; для param-пина проба только у модуляторов | drop кабеля стоил ~960 мс (Debug) | my/quick-fixes |
| src/app/Startup.cpp | `// fork:` экспорт NvOptimusEnablement/AmdPowerXpress (_WIN32) | дискретная GPU на ноутбуках | my/fps |
| src/core/GraphNode.h | `showMiniViewport` по умолчанию false | мини-вьюпорты дорогие | my/fps |
| src/core/NodeViewport.{h,cpp} | троттлинг 20 Гц, `LastColorTexture()` | FPS | my/fps |
| src/app/panels/ViewportPanel.cpp | пропуск невидимых, Preview quality | FPS | my/fps |
| src/nodes/{AnalyzeNodes,PaletteNode,PredictiveColoringNode}.{h,cpp} | `PboReadback` вместо glReadPixels | убрать sync-стоп | my/fps |
| src/app/panels/SettingsWindow.cpp, src/app/graph/PatchIO.cpp | настройка Preview quality + предупреждение GPU | диагностика | my/fps |
| src/app/frame/StageMenuBar.cpp, StagePresent.cpp | тултип GPU; `INFINITE_FPS_LOG` | диагностика | my/fps |
| новые: src/core/{PboReadback,PreviewQuality,GpuDiag}.h | наш код | | my/fps |
| `src/core/chat/AgentStream.h/.cpp` | `AgentKind::Custom`; `TurnOptions.customBaseUrl/customKeyEnv`; три `if` в `AgentArgs` вынесены в `ArgsClaude/Codex/Antigravity`, `AgentArgs` идёт через таблицу | Рефакторинг `ProviderDef` (этап 6.2) | my/agents |
| `src/core/chat/AgentAuth.cpp` | `case Custom` в Sign-in/Status/Sign-out, Custom в ветке Codex у `SignedInFrom` | Статус «ключ задан» через shell | my/agents |
| `src/core/CliSetup.h/.cpp` | строка `custom` в `Agents()` (пустые команды), поля `customBaseUrl/customKeyEnv/customPreset` в `Config` + load/save, ранний возврат `custom` в `AgentReadiness` | Свой провайдер | my/agents |
| `src/app/panels/ChatPanel.cpp` | `KindOf` через `chat::FindProvider`; `to.custom*` и проверка формы в `StartRun`; список моделей для Custom в `ModelsOf`; цвет аватара | Свой провайдер | my/agents |
| `src/app/panels/CliSetupSection.cpp` | вызовы `DrawCustomAgentForm()`, `DrawConnectAgentForm()` | Формы в Settings | my/agents |
| `src/app/panels/SettingsWindow.cpp` | два места `custom-agent`, `connect-agent` в `kPlaces` | Прыжок к формам из терминала/чата | my/agents |
| `src/core/chat/AgentRun.h/.cpp` | `Options::work` — турн в процессе вместо PTY-потомка | Встроенный агент | my/agents |
| `src/core/RemoteControl.h/.cpp` | `RemoteControl::Call` — вызов из любого потока через ту же очередь | HTTP-MCP и встроенный агент | my/agents |
| `src/app/Startup.cpp` | `McpHttp::StartFromSettings()` после `RemoteControl::Start` | HTTP-MCP | my/agents |
| `CMakeLists.txt`, `src/app/selftest/ChatTests.cpp` | новые исходники (`Providers`, `HttpAgent`, `McpHttp`, формы, `AgentNet*`), `-framework Security`; тесты | | my/agents |
| `src/app/graph/Rpc.cpp` | `create_node`: пустая `category` резолвится из `NodeFactory::CategoryOf`; `validate_patch_text`: `E_BAD_KEY` «could not be drawn here» не ошибка, а счётчик `deferredKeys` | round-trip `save_patch` → `load_patch_text`; валидатор не рубит штатные патчи с `mod` по имени контрола | tu/api-roundtrip |
| `src/app/graph/PatchIO.cpp` | `BuildPatchData`: пустая категория ноды резолвится из имени типа перед записью | не писать `node 14  Set Vertex Color` (двойной пробел теряет ноду при загрузке) | tu/api-roundtrip |
| `src/core/PatchSchema.cpp` | `Validate`: узел с непустой категорией без пробела и неизвестным типом проверяется как «категория + тип»; новый код `E_MISSING_CATEGORY` | назвать уже сохранённый битый файл, а не молча выбросить ноду | tu/api-roundtrip |
| `src/app/graph/RpcTerminal.cpp` | `transport`: действие `loop` (`enabled`, `start`, `end` в битах) через `ArrangeSetLoop`; в ответ добавлены `loopEnabled/loopStart/loopEnd`. `bind_mod`/`unbind_mod`/`set_expr`: один читатель узла (`dst`/`index`/`node`/`target`, число / строка / `{index}`), `src`/`source`/`from`/`modulator`, `output`/`out`, ключ `key`/`param`/`name`/`control`; внятные ошибки; `unbind_mod` не кладёт пустой шаг undo | API-7: диапазон лупа программно; k17 (API-6): привязка модуляции через API | tu/api-roundtrip, tu/bind-mod-rpc |
| `src/app/graph/NodeRegistry.cpp` | `ArrangeSetLoop`: при `enabled` диапазон короче `kPPQ/16` поднимается до неё | луп не «включён», но нулевой (Transport такой отбрасывал — кнопка выглядела мёртвой) | tu/api-roundtrip |
| `src/app/selftest/MiscTests.cpp` | в `RunRpcBatchTest` проверки round-trip категории, `deferredKeys`, действий `transport loop`; блок k17: `bind_mod`/`unbind_mod` всеми написаниями узла и ключа, `mod`-строка в сохранении, отказы (узел/ключ/`src`), отсутствие пустого undo | регрессии по трём карточкам; API-6 | tu/api-roundtrip, tu/bind-mod-rpc |
| `src/platform/AppPaths.h` | `sPortableDataDir()` / `FileExists()` / `DirName()` / `ResolvePortableDataDir(exePath)`; `AppSupportDir()` в начале отдаёт портативную папку, если она задана | портативная копия держит всё состояние рядом с собой | tu/api-roundtrip |
| `src/app/Startup.cpp` | одна строка в начале `InitApp`: `AppPaths::ResolvePortableDataDir(Platform::ExecutablePath())` | решить до первого чтения настроек/автосохранения/лога | tu/api-roundtrip |

Наши новые файлы (конфликтов с апстримом нет): `src/core/chat/{Providers,HttpAgent}.*`, `src/core/{McpHttp.*,McpAgentDoc.h}`, `src/platform/AgentNet*.{h,mm,cpp}`, `src/app/panels/{CustomAgentForm,ConnectAgentForm}.cpp`, `tools/{mcp_http_probe,mock_openai_server,gen_agent_doc}.py`, `docs/{custom-agent,connect-agent}.md`.

## Правила
- Наш код — в **собственных файлах** (новые .cpp/.h), а не внутри файлов апстрима.
- В файлах апстрима — только короткие «крючки» (вызов/include) с комментарием `// fork:`.
- `AppShared.h` без крайней необходимости не трогаем (горячий файл, конфликты почти гарантированы).
- Каждая правка файла апстрима заносится в таблицу выше — по ней разрешаются конфликты при синхронизации.

## Infinite TU: отдельная программа рядом с официальным Infinite (2026-11-12)

Все имена собраны в `src/core/ForkIdentity.h` (`// fork:` в местах использования). Что разделено:
- exe: `InfiniteTU.exe` + `InfiniteTU.com` (CMake `OUTPUT_NAME`), ProductName/InternalName/OriginalFilename в `.rc.in`, заголовок окна «Infinite TU».
- Данные/настройки/кэши/логи: `%APPDATA%\InfiniteTU` (macOS `~/Library/Application Support/InfiniteTU`, Linux `~/.config/InfiniteTU`), лицензионная папка тоже (`License.cpp`), `SettingsDirForVST3` (`PluginVST3.mm`).
- Mutex одного экземпляра: `Local\InfiniteTU.SingleInstance`; AppUserModelID `InfiniteTU.Synthesizer`.
- Порты: control 7777→7877, OSC 9100→9200, MCP 7790→7890 (`InfiniteCli` по умолчанию 7877).
- Credential Manager/Keychain service: `InfiniteTU`.
- Установщик `.iss`: новый AppId (GUID), `{localappdata}\Programs\InfiniteTU`, ярлыки «Infinite TU», `InfiniteTU-Setup-*.exe`; компонент VST3 удалён (иначе затёр бы плагины официальной версии в общей папке VST3).
- macOS: bundle id `com.deriio.infinitetu`, bundle name «Infinite TU».
- Проверка обновлений отключена (`UpdateCheck::Start`), иначе предложила бы заменить форк официальной сборкой.
- CI (`build.yml`, Windows): пути `Infinite.exe/.com` → `InfiniteTU.*`; SignPath и так условный по секрету.
- Не менялось (не проверено, что конфликтует): ассоциацию `.infinite` программа сама не регистрирует (только по кнопке в настройках, ключи `Infinite.Patch`); скрипты настройки агента (`%LOCALAPPDATA%\Infinite\node|bin`, путь `C:\Program Files\Infinite\Infinite.exe` в `CliSetup.cpp`) — общие с официальной, пока форк их не использует; MIDI/NDI-имена «Infinite».
