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
