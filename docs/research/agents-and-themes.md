# Агенты и темы: исследование (только чтение кода)

Дата: 2026-11-11. Правок кода нет. Пути — от корня репозитория. Значения ключей API в отчёт не копировались (в коде их и нет).

---
## Тема A — кастомные ИИ-агенты

### A1. Что такое «агенты» фактически
**Это НЕ HTTP-вызовы API, а внешние CLI-программы**, запускаемые подпроцессом по одному на сообщение чата.
- Три агента, `enum class AgentKind { Claude, Codex, Antigravity }` — `src/core/chat/AgentStream.h:11`. В UI «Gemini» — это на деле **Antigravity (`agy`)**, а «ChatGPT» — **Codex (`codex`)**; id: `claude`/`codex`/`antigravity` (`src/app/panels/ChatPanel.cpp:412-422`, `KindOf`).
- argv на ход: `claude -p …`, `codex exec --json …`, `agy --output-format stream-json …` (`src/core/chat/AgentStream.cpp:282-330`; модель — `--model`/`-m`).
- Запуск: `AgentRun::Start` через PTY (`src/core/chat/AgentRun.cpp:42-140`), через login-shell `exec "$@"`; stdout (stream-json) парсится построчно в `AgentEvent` (`AgentStream::Feed`, `AgentStream.cpp:~195`). Старт хода — `StartRun` (`ChatPanel.cpp:1247-1279`).
- Агент меняет патч сам, вызывая CLI `infinite …` (и MCP-сервер `infinite-engine`, регистрируется при «Set up»: `CliSetup.cpp:91-127`, поля `mcpAdd`, `skillsDir`). Приложение отдаёт агенту «Briefing» + карточки изменений с Undo.
- Что держится на агентах: только **Chat-панель** (`ChatPanel.cpp`, 5583 строк), онбординг (`src/core/chat/Onboarding.cpp`), Settings → «Agents & Terminal» (`src/app/panels/CliSetupSection.cpp`), установка/логин (`src/core/CliSetup.cpp`, `chat/AgentAuth.cpp`). Короткие команды (add/connect/set/undo) исполняются локально без агента (`PatchGrammar.cpp`). Остальные функции приложения от агента не зависят.
- README «No agent ships with Infinite» (стр. 129) верно: агент ставится кнопкой «Set up» (vendor-инсталлятор / `npm i -g @openai/codex` в свой Node), логин — у самого CLI. docs/CHAT.md: «Infinite never asks for or stores an API key».
- Вывод: «раздел Агенты» в Settings реально есть (вкладка «Agents & Terminal», секция «Agents», `SettingsWindow.cpp:97`, `:1810`), но это управление CLI, а не провайдеры API.

### A2. UI, конфиг, секреты
- UI: `src/app/panels/CliSetupSection.cpp:64` `DrawCliSetupSection()` — строка на агента (`Agents()` цикл, :111), статус и кнопка; модели/использование/папки/команды ниже. Вкладка в `SettingsWindow.cpp:42,97`.
- Выбор модели: пилюля в Chat-панели (`ChatPanel.cpp:~4614-4790`: `ModelsOf`, `FixedModels`, `ParseCodexModels`, `ParseAgyModels` — списки берутся у самих CLI/их кэшей).
- Конфиг: `CliSetup::Config` (`src/core/CliSetup.h:94-114`: agent, workspace, folderAccess, `models` map, folders, commands, usage) → JSON `<AppSupportDir>/cli-setup/config.json` (`CliSetup.cpp:217`, `SaveConfig` :851, nlohmann). Singleton `CliSetup::Prefs()` (:1276). Чаты — `<…>/<id>.json` (`ChatThread.cpp:205`).
- Секреты: **Infinite их не хранит вообще**. Нет Keychain/Credential Manager/libsecret (grep по SecItem/CredRead/libsecret — только упоминания в тексте про Gemini). «Signed in» определяется косвенно: `~/.claude.json` содержит `oauthAccount` или есть env `ANTHROPIC_API_KEY`; `~/.codex/auth.json` или `OPENAI_API_KEY` (`CliSetup.cpp:1082-1084`). → для кастомного эндпоинта хранилище секретов придётся создавать с нуля.

### A3. HTTP / JSON / абстракция провайдера
- JSON: есть, `external/json/json.hpp` (nlohmann, 24765 строк, используется в 38 файлах src).
- HTTP: только `Platform::HttpGet(url, ua, body, err, timeout)` — **GET, без заголовков, без POST, без стриминга**, ограничение размера (`src/platform/Platform.h:1640`; реализации: `Platform.mm:8644` (macOS), `win/PlatformWin.cpp:1338` (WinHTTP), `linux/PlatformLinux.cpp:1036` (динамический libcurl)). Используется для апдейтов и каталога расширений. Для чата нужен POST+Authorization+SSE — **расширять придётся на 3 платформах**.
- Абстракции провайдера нет: «провайдер» = `AgentKind` enum + `if/else` по kind в `AgentArgs` (`AgentStream.cpp:282-330`), `KindOf` (`ChatPanel.cpp:412`), таблица `CliSetup::Agents()` с shell-командами установки/MCP, захардкоженные `AgentLogos`, `kFamily` цвета аватаров (`ChatPanel.cpp:445`), `ModelsOf`. Четвёртый добавляется копированием по всем этим местам.

### A4. Что добавить для OpenAI-совместимого провайдера
**Вариант 1 — через CLI (подпроцесс, как сейчас).** Нужна CLI, умеющая в OpenAI-совместимый endpoint **и** вызывать `infinite`/MCP:
- Codex поддерживает кастомный provider через `~/.codex/config.toml` (`model_providers`, base_url, env_key) — **не проверено по документации/коду**; в репозитории нет работы с этим, только чтение `model` (`ConfiguredModel`, `AgentAuth.h:70`). Тогда: запись секции в config.toml (TOML-запись отсутствует — писать вручную), ключ передавать через env процесса (`AgentRun::Options.env`, `AgentRun.cpp` уже поддерживает), argv `codex exec --json -m <model> -c model_provider=…`. Плюс: весь стек (парсинг stream, MCP, карточки, Undo) работает как есть. Минус: зависимость от Codex CLI и его формата конфига; Ollama/DeepSeek/OpenRouter поддерживаются лишь настолько, насколько Codex умеет Chat Completions (**не проверено**).
- Изменения: новое поле конфига (endpoint list), UI-форма Base URL/API Key/Model ID в `CliSetupSection.cpp`, передача env в `StartRun`, расширение `KindOf`/`AgentArgs`, списки моделей (фиксированный из поля Model ID).

**Вариант 2 — встроенный HTTP-клиент + свой агентный цикл.**
- Нужно: (а) POST + заголовки + стриминг SSE в `Platform` на 3 ОС (или подключение libcurl/cpp-httplib), (б) цикл tool-calling: описать инструменты `infinite_*` (сейчас живут в MCP-пакете `tools/npm/infinite-engine` и в CLI `infinite`; внутри приложения есть RPC `src/app/graph/RpcApp.cpp`) в формате OpenAI `tools`, исполнять вызовы локально, возвращать результаты, (в) эмулировать `AgentEvent` (Session/Text/Tool/ToolResult/End, `AgentStream.h:22`) и `TurnUsage`, (г) хранить ключ — Keychain/CredMan/libsecret или хотя бы файл с правами 0600, (д) контекст/сессию вести самим (у CLI `--resume` даёт сессию, здесь её нет), (е) права/папки/команды (`TurnOptions`) реализовать самим — сейчас их обеспечивает CLI, (ж) Briefing/skills (`AgentDocs.h` 3771 строк) подавать как system prompt.
- Плюс: независимость от CLI, любой OpenAI-совместимый endpoint. Минус: по сути новая подсистема агента.

### Выводы для реализации (A)
- Затрагиваемые файлы: `src/core/CliSetup.h/.cpp` (Config, Agents), `src/core/chat/AgentStream.h/.cpp`, `AgentRun.cpp`, `AgentAuth.*`, `src/app/panels/CliSetupSection.cpp`, `ChatPanel.cpp` (KindOf, аватары, модели, StartRun), `Onboarding.cpp`; для варианта 2 дополнительно `src/platform/Platform.h`, `Platform.mm`, `win/PlatformWin.cpp`, `linux/PlatformLinux.cpp`, новый модуль агентного цикла.
- Объём: вариант 1 — **M** (при условии, что Codex реально берёт кастомный провайдер — не проверено); вариант 2 — **L** (HTTP-стек ×3 ОС, tool-цикл, секреты, права).
- Риски: нет абстракции провайдера (рефакторинг `AgentKind` в данные — желательно первым шагом, **S–M**); хранение ключей (первое в проекте); разные модели tool-calling у Ollama/DeepSeek/OpenRouter; i18n строк (6 языков, `T()`/`I18N_KEY`) и автотесты `app/selftest/ChatTests.cpp`; «Infinite never stores an API key» — позиционирование README/docs придётся менять.
- Рекомендация: сначала рефакторинг в `struct ProviderDef` (id, name, kind: cli|http), затем вариант 1 как быстрый путь, вариант 2 — отдельным этапом.

---
## Тема B — пользовательские темы

### B1. Встроенные темы
- Структура: `struct Preset { name; Table categories; UiTheme ui; }` (`src/core/CategoryColors.cpp:23-29`). `UiTheme` (`CategoryColors.h:18-26`) = **6 цветов**: `windowBg, panelBg, text, textDim, border, accent`. Плюс 11 цветов категорий нод (Source, 3D, Compositing, Effects, Modulators, Prediction, Macros, Utility, Notes, Synths, AudioEffects; `CategoryNames()`) на пресет и 5 цветов кабелей (`CableType`, 5).
- Тем **15**: Infinite, Nord, Dracula, Catppuccin Mocha, Tokyo Night, Catppuccin Latte, GitHub Light, Solarized Light, Nord Light, Forest Green, Infinite Midnight/Deep/Ember/Mist/Paper (`RawPresets()` :35+; список с первыми «Infinite*» — `Presets()` :~395-420, по умолчанию Ember). Светлая/тёмная определяется по яркости `windowBg` (`IsThemeLight`, `CategoryColors.cpp:628`).
- Всё остальное выводится из этих 6 цветов в `ApplyTheme()` (`src/app/ui/Theme.cpp:370`, ~61 `ImGuiCol_*`), производные через shade/mix. Тексты автоматически подтягиваются до контраста 4.5:1 (`Presets()`).
- Отдельно — дизайн-токены `src/app/ui/design/tokens.json` (5716 строк, генерируется в `Tokens.gen.h` скриптом `tools/design/build_tokens.py`): роли с парами dark/light (`tok::U32(tok::x, light)`), ~63 пары dark/light — это фиксированные роли, **не зависящие от пресета**.
- Переключение: Settings, выпадающий список с превью-свотчами (`SettingsWindow.cpp:229` `SetPreset(i)`, :237 `drawThemeSwatches`); также RPC (`RpcApp.cpp:260`) и env `INFINITE_THEME`.

### B2. Централизация
- Вне темы (без selftest/FrameTests/gen): `IM_COL32` ≈117, `ImVec4(` ≈235, `ImColor` ≈20 вхождений (с тестами всего 380; часть — легитимные производные/альфы от токенов, **точная доля литералов не проверена**).
- Тяжёлые файлы (число строк с этими конструкциями): `ui/design/components/LibraryParts.h` 26, `panels/PerfPanel.cpp` 21, `panels/ArrangePanel.cpp` 18, `core/SplashScreen.h` 17, `components/FormParts.h` 17, `bodies/PreviewBodies.cpp` 17, `components/FieldWell.h` 14, `panels/ChatPanel.cpp` 14, `ui/Theme.cpp` 13, `bodies/OscBodies.cpp` 13.
- Вывод: централизация хорошая — прямые обращения к `windowBg/panelBg` единичны (Theme.cpp 12/20, CategoryColors 3/6, StageNodeBodies 3, Minimap 1), остальное идёт через ImGui-стиль и токены. Но токены `tokens.json` фиксированы и пресетом не меняются — в кастомной теме они останутся «родными».

### B3. «Второй тёмный цвет» в существующих токенах
Однозначного токена нет; ближайшие кандидаты (по комментариям `Theme.cpp:208-318`):
- `windowBg` — фон холста/окна, меню-бар, заголовки (`TitleBg*`, `MenuBarBg`, `ScrollbarBg`);
- `panelBg` — закреплённые панели, ноды (база tint нод), поп-апы, `FrameBg` через shade (`Theme.cpp:223,280,428-434`);
- сетка/рейка/скоуп — фиксированные токены (`scope_*`, `backdrop_*`) или производные от `windowBg` (`Theme.cpp:306-318`).
Т.е. «основной фон» = `windowBg`, «второй тёмный» по смыслу уже = `panelBg` (в каждой тёмной теме он на шаг светлее). Если владелец хочет третий слой (например, рейка/статусбар/фон нод отдельно) — нужен **новый токен** в `UiTheme`. Уточнить у владельца, какую именно поверхность он имеет в виду.

### B4. Папка конфигурации и формат
- `AppPaths::SettingsDir()`/`AppSupportDir()` (`src/platform/AppPaths.h:201-262`): macOS `~/Library/Application Support/Infinite`; Windows `%APPDATA%\Infinite`; Linux `$XDG_CONFIG_HOME/Infinite` либо `~/.config/Infinite`; в режиме плагина — подпапка `Plugin`.
- Форматы: **выбор темы** — файл `Infinite.theme` из одной строки с именем пресета (`CategoryColors.cpp:441-452, 595-607`); **переопределения цветов** — самодельный `key=r,g,b` текст `Infinite.appearance` (`:960-1000`); настройки агентов — JSON (nlohmann). Единого формата настроек нет; JSON-библиотека есть.

### B5. Импорт/экспорт/редактор
- Не найдено: grep по export/import theme/json theme — пусто. Есть частичный «редактор»: поштучные оверрайды цветов категорий и кабелей, прозрачность/скругление нод (`SetCategoryColor`, `SetCableColor`, `SetNodeOpacity` в `CategoryColors.h:51-90`) — но не 6 цветов `UiTheme`, и они хранятся отдельно поляризационно (dark/light).

### Выводы для реализации (B)
- Файлы: `src/core/CategoryColors.cpp/.h` (загрузка JSON-тем из `SettingsDir()/themes/*.json` и слияние в `Presets()`; сейчас `Presets()` — статический `const` список → сделать изменяемым/перезагружаемым; сохранение выбора по имени уже есть), `src/app/panels/SettingsWindow.cpp` (список + кнопки Import/Export/New, редактор цветов через ImGui ColorEdit), возможно `src/app/ui/Theme.cpp` (если добавляется новый токен «второй тёмный»), `RpcApp.cpp` (не обязательно). Схема JSON: name, base (light/dark), 6 цветов UiTheme (+ необязательно categories/cables); недостающее наследовать от выбранной встроенной темы.
- Объём: загрузка/сохранение JSON + выбор в списке — **S–M**; встроенный редактор с живым превью — **M**; если «второй цвет» = новый токен, протащенный в `ApplyTheme` и в места с литералами — **M–L**; полная перекраска фиксированных токенов `tokens.json` под пользовательскую тему — **L** (не проверено, сколько мест).
- Риски: контраст (авто-подтяжка текста уже есть, применять и к пользовательским); авто-определение light/dark по `windowBg`; плагин-режим (отдельный `SettingsDir`); фиксированные токены и ~117+235+20 литералов цвета не подхватят пользовательскую тему; валидация поврежденного JSON; автотесты `UiAudit.cpp`/`MiscTests.cpp:745` перебирают пресеты по индексам.
