# Реестр изменений форка в коде апстрима

| Файл | Что изменено | Зачем | Ветка |
|------|--------------|-------|-------|
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

Наши новые файлы (конфликтов с апстримом нет): `src/core/chat/{Providers,HttpAgent}.*`, `src/core/{McpHttp.*,McpAgentDoc.h}`, `src/platform/AgentNet*.{h,mm,cpp}`, `src/app/panels/{CustomAgentForm,ConnectAgentForm}.cpp`, `tools/{mcp_http_probe,mock_openai_server,gen_agent_doc}.py`, `docs/{custom-agent,connect-agent}.md`.

## Правила
- Наш код — в **собственных файлах** (новые .cpp/.h), а не внутри файлов апстрима.
- В файлах апстрима — только короткие «крючки» (вызов/include) с комментарием `// fork:`.
- `AppShared.h` без крайней необходимости не трогаем (горячий файл, конфликты почти гарантированы).
- Каждая правка файла апстрима заносится в таблицу выше — по ней разрешаются конфликты при синхронизации.
