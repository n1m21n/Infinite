# Свой агент и подключение внешних агентов к Infinite

Дата: 2026-11-12, ветка `my/agents`. **Не проверено** помечено явно. Сборка и проверка на macOS (Intel);
Windows-код написан, но не собирался и не запускался.

## 0. Что такое «Agents & Terminal» (простыми словами)
Раздел Settings → Agents & Terminal и чат Infinite работают так: в самом приложении **нет** ИИ.
«Агент» — это отдельная программа командной строки, которую вы ставите кнопкой «Set up»:
**claude** (Claude Code, в UI «Claude»), **codex** (в UI «ChatGPT»), **agy** (Antigravity, в UI «Gemini»).
Infinite запускает такую программу на каждое сообщение в чате, она сама логинится у своего вендора
(через браузер), а Infinite даёт ей команду `infinite` и «инструкцию» (Briefing), чтобы она меняла ваш патч:
добавляла ноды, соединяла, крутила параметры. Карточки изменений в чате и «Undo» — это Infinite
сравнивает патч до и после. Если у вас нет подписки/доступа к этим сервисам (регион), эти три строки вам
не нужны — используйте «Custom» ниже.

## 1. «Custom» — встроенный агент на вашей модели (OpenAI-совместимый API)
Четвёртая строка в том же разделе: никаких внешних программ, чат сам ходит на ваш сервер.

Settings → Agents & Terminal → **Custom agent**:
1. **Preset** — Ollama / DeepSeek / OpenRouter: подставляет только Base URL; имена моделей — подсказки в пилюле модели чата.
2. **Base URL** — адрес до `/v1` (приложение добавляет `/chat/completions`).
3. **Model ID** — как называет модель сервис (`deepseek-chat`, `qwen3-coder`, `vendor/model` у OpenRouter).
4. **API key** — вставить и нажать «Save key». Ключ уходит в системное хранилище (macOS Keychain / Windows
   Credential Manager) и **не пишется** ни в конфиги Infinite, ни в логи; в окне виден маской (`sk-…a1b2`).
   «Remove» стирает. Запасной вариант — переменная окружения (`CUSTOM_LLM_API_KEY` по умолчанию, читается
   через login-shell: `export …` в `~/.zprofile`).
5. **Test connection** — отправляет крошечный запрос и пишет, что ответил сервер.
Затем в чате `@custom …` или выберите Custom агентом по умолчанию.

### Как работает (для справки)
`POST {Base URL}/chat/completions`, `stream: true` (SSE), инструменты Infinite передаются в формате OpenAI
`tools`: `infinite_context`, `infinite_cli`, `infinite_rpc`, `infinite_rpc_methods` — те же, что у MCP
(§2), выполняются локально, результат возвращается модели; не более 12 шагов за ход. Если сервер/модель
отвечает 400/404/422 про tools, чат продолжается **без инструментов** (модель отвечает, но патч не меняет),
в чате об этом одна строка. История диалога хранится в памяти процесса (после перезапуска приложения
модель не помнит прошлые реплики; карточки и Undo остаются).

### Ollama (локально) — не проверено с реальным Ollama
Base URL `http://localhost:11434/v1`, ключ пустой. `ollama pull qwen3-coder`, Model ID — то же имя.
Для изменения патча нужна модель с tool calling; иначе только разговор.

### DeepSeek — не проверено с реальным ключом
Base URL `https://api.deepseek.com`, модель `deepseek-chat` (поддерживает tools по документации DeepSeek —
не проверено). Ключ из кабинета платформы DeepSeek.

### OpenRouter — не проверено с реальным ключом
Base URL `https://openrouter.ai/api/v1`, Model ID — полное имя `vendor/model` с openrouter.ai/models; не у всех
моделей есть tools.

### Проверено (без реального ключа)
Локальный мок-сервер `tools/mock_openai_server.py` (SSE, tool_call, 401, режим «без tools»):
`python3 tools/mock_openai_server.py 18080 test-key`; в форме Base URL `http://127.0.0.1:18080/v1`,
Model ID любой, ключ `test-key` (тестовое значение). Результаты — в `docs/screenshots/` и в отчёте ветки.

## 2. Подключить Infinite к внешнему агенту (MCP)
Что было: пакет `infinite-engine` — stdio-обёртка на Node, умеет только headless-рендер/валидацию через
бинарь `infinite`, **к запущенному приложению не подключается**.
Что добавлено: MCP-сервер **внутри приложения**, Streamable HTTP (JSON-ответы), `127.0.0.1:<порт>`, токен.
Settings → Agents & Terminal → **Connect Infinite to an outside agent**:
- **Turn on / Turn off**, **Port** (по умолчанию 7790), **Access token** (скрыт, Show/New token). По умолчанию выключено.
- **Status** — «Available at http://127.0.0.1:7790/mcp» после реальной самопроверки (запрос на свой порт).
- Кнопки копирования: **инструкция для агента** (это `docs/connect-agent.md` с подставленными URL/токеном/путём;
  вставьте её своему агенту — он подключится сам), команда Claude Code, JSON `mcpServers`, `config.toml` Codex.
Инструменты: `infinite_context` (состояние патча), `infinite_cli` (команды `add/connect/set/undo/find/docs…`),
`infinite_rpc` (низкоуровневый RPC приложения), `infinite_rpc_methods`. Node не нужен.
Токен — секрет (лежит в `~/Library/Application Support/Infinite/mcp-http.json`, права 0600; это не API-ключ модели).
Проверка клиентом: `python3 tools/mcp_http_probe.py <port> <token> [tool [json]]`.

## Не сделано / ограничения
- Linux: встроенный агент выдаёт сообщение «not available on Linux yet».
- Windows: `infinite_cli` реализован (`src/core/McpHttp.cpp`, `RunCli` через `_popen`) и `infinite_context`
  переведён на внутрипроцессный RPC, но на живой сборке не проверены; `AgentNetWin.cpp`
  (WinHTTP, Credential Manager) — тоже не проверены.
- Старый SSE-транспорт MCP (HTTP+SSE 2024-11-05) не реализован — только Streamable HTTP без серверных уведомлений.
- «Отвечает после перезапуска» без памяти диалога (см. выше).
