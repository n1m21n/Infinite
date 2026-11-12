# Реестр изменений форка в коде апстрима

| Файл | Что изменено | Зачем | Ветка |
|------|--------------|-------|-------|
| _(пока пусто)_ | | | |
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
