# Changelog форка

## 2026-11-12 — релизная сборка my/main
- Слиты ветки: my/themes (JSON-темы), my/osc (OSC/Bitwig), my/quick-fixes (подсказки, фильтр меню по портам), my/fps, my/agents (встроенный агент + MCP по HTTP).
- Не слито: my/inspector (дорабатывается отдельно).
- Конфликты: только docs/fork-changes.md, оставлены обе стороны.
- Сборка: scripts/ibuild.py build --mode dev (x86_64) — OK. Тест: 60/60 (драйвер ищет build/, а сборка в build-dev/ — запускали с INFINITE_BIN).

## 2026-11-13 — Infinite TU v0.2-fork (Windows, тестовая)
- `origin/my/inspector` влит в `my/main` (--no-ff, конфликтов нет); коммит сборки `a4bf83500`.
- Локально: ibuild dev (x86_64) OK, `driver.sh --skip-build --fast` 60/60 (INFINITE_BIN = build-dev/InfiniteTU.app/Contents/MacOS/InfiniteTU).
- Windows x64: Actions run 43340392953 — успех; Design ratchet красный (так же и на main, не от форка).
- Релиз: https://github.com/deriio/Infinite-TU/releases/tag/v0.2-fork, asset `InfiniteTU-windows-x64.zip` (35 МБ), без установщика (VST-имена совпали бы с официальным). Запуск на ПК с GTX 1050 Ti — не проверено.

## 2026-11-13 — tu/api-roundtrip: категория ноды, mod-строки, транспортный луп

Ветка `tu/api-roundtrip` от `tu/main`. Три бага с доски, найденные при работе над форком.

- **API-1 — `save_patch` терял категории нод.** `create_node` читал `category` как необязательный
  и не проверял; пустая категория писалась как `node 14  Set Vertex Color` (двойной пробел),
  а читатель разбирал это как категорию `Set` и тип `Vertex Color`, после чего **молча**
  выбрасывал ноду. Отсюда «`load_patch_text` вернул 0 нод». Правки: резолв категории в
  `create_node`, фолбэк в `BuildPatchData`, новый `E_MISSING_CATEGORY` с готовой исправленной
  строкой для уже сохранённых битых файлов.
- **API-2 — `validate_patch_text` рубил штатные патчи с `mod` по имени контрола.**
  `paramIndexOfKey` возвращает `-2`, когда таблица ключей ещё не заполнена (в оконном
  приложении её нечем заполнить), а валидатор считал это ошибкой `E_BAD_KEY`. Загрузчик ту же
  ситуацию откладывает (`PendingKeyed`) — теперь валидатор ведёт себя так же и отдаёт
  `deferredKeys`. Побочно: проверка кабелей/циклов раньше не выполнялась вовсе, теперь выполняется.
- **UX-1 / API-7 — луп.** У `transport` не было действия `loop`; диапазон задавался только
  перетаскиванием в UI. Добавлено `transport {action:"loop", enabled, start, end}` (биты) и
  минимальный диапазон в `ArrangeSetLoop`, чтобы «включённый, но нулевой» луп не появлялся.
  Кнопка в Piano Roll при пустом выделении намеренно возвращает понятную ошибку, а не молчит.
- **Проверено.** Полная сборка Windows x64 (VS 2022 + CMake, `-A x64`, Release) — OK.
  Штатный `--validate` на реальном файле из сессии: было `E_UNKNOWN_TYPE: 'Vertex Color'`,
  стало `E_MISSING_CATEGORY` с указанием исправленной строки. Сквозной RPC-прогон
  (`create_node` без категории → `save_patch` → `load_patch_text`, `validate_patch_text`,
  `transport loop`) — 15/15. Headless self-tests: DSPTEST, AUDIOPDCTEST, SYPHONPATCHTEST,
  UNICODEPATHTEST, RESTARTTEST, CHORDPROGTEST, PERFMATRIXTEST, RECSYNCTEST — все pass.
- **Не проверено.** Кнопка лупа в Piano Roll и полоса на линейке руками в приложении не
  проверялись (нужен интерактивный сеанс) — правки на этот раз только в транспорте и модели,
  UI не трогал. `INFINITE_RPCBATCHTEST` и остальные frame-тесты требуют окна и в headless-режиме
  не гоняются: новые проверки добавлены в `RunRpcBatchTest`, их нужно прогнать через
  `driver.sh` на машине с GPU.

