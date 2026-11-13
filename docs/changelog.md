# Changelog форка

## 2026-11-12 — релизная сборка my/main
- Слиты ветки: my/themes (JSON-темы), my/osc (OSC/Bitwig), my/quick-fixes (подсказки, фильтр меню по портам), my/fps, my/agents (встроенный агент + MCP по HTTP).
- Не слито: my/inspector (дорабатывается отдельно).
- Конфликты: только docs/fork-changes.md, оставлены обе стороны.
- Сборка: scripts/ibuild.py build --mode dev (x86_64) — OK. Тест: 60/60 (драйвер ищет build/, а сборка в build-dev/ — запускали с INFINITE_BIN).
