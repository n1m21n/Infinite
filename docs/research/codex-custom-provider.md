# Codex CLI: кастомный провайдер (этап 6.1), проверка по документации

Дата: 2026-11-12. Практический запуск не делался: Codex CLI на машине не установлен.

## Выводы
1. **Поддержка есть.** Провайдер задаётся таблицей `model_providers.<id>` с полями `base_url`, `env_key` (имя переменной с ключом), `wire_api`; выбирается `model_provider = "<id>"`. Свои id не могут быть `openai`, `ollama`, `lmstudio`, `amazon-bedrock`, `amazon-bedrock-runtime`, `gcp` (зарезервированы).
2. **Флаги `-c` работают для любого ключа**: `-c key=value`, значение разбирается как TOML, точечная запись задаёт вложенные ключи (`mcp_servers.x.enabled=false`). Это позволяет не править `config.toml`. Пример именно для `model_providers` в документации не показан — **применение `-c model_providers.<id>.base_url=…` выведено из этого правила, не проверено запуском**.
3. **`wire_api = "chat"` удалён.** Мейнтейнеры объявили (9 дек 2025) отказ от `chat/completions`, удаление — начало февраля 2026 (предупреждение → жёсткая ошибка; в обсуждении упомянут PR #10157). Остаётся `responses`. Следствие: Ollama — только если его версия отдаёт `/v1/responses`; DeepSeek (Chat Completions) — только через прокси; OpenRouter заявляет Responses API (бета).
4. **MCP-инструменты**: MCP в Codex настраивается отдельно (`mcp_servers`, `codex mcp add`) и не привязан к провайдеру. Работает ли вызов инструментов — зависит от модели/эндпоинта (function calling через Responses). **Не проверено.**

## Решение
Запуск реализован через `-c` (флаги в `src/core/chat/Providers.cpp`), `wire_api="responses"` зашит. Если ваш эндпоинт без Responses API — нужен прокси.

## Источники
- Конфиг Codex, раздел Model providers и `-c`: https://learn.chatgpt.com/docs/config-file/config-advanced (бывш. developers.openai.com/codex/config-advanced)
- Объявление об удалении chat wire API: https://github.com/openai/codex/discussions/7782
- Сторонние описания (не официальные): https://codex.danielvaughan.com/2026/04/23/codex-cli-custom-model-providers-configuration-guide/ ; https://github.com/janhq/jan/issues/7413
