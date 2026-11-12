# Реестр изменений форка в коде апстрима

| Файл | Что изменено | Зачем | Ветка |
|------|--------------|-------|-------|
| _(пока пусто)_ | | | |
| `src/core/CategoryColors.{h,cpp}` | `Presets()` → изменяемый список (резерв 200 мест), поля `cables/user/file/base` в `Preset`, API `SetUserPresets/IsUserPreset/FindPreset/...`, кабели темы в `DefaultCableColor`, `PresetNames` пересобирается | JSON-темы (этап 4а) | my/themes |
| `tools/design/build_tokens.py`, `Tokens.gen.h` | цветовые роли `inline` + дефолты + реестр `kColorTokens`; генерация `docs/theme-tokens.md` | переопределение токенов (4б) | my/themes |
| `src/app/Startup.cpp`, `frame/StageFramePump.cpp`, `panels/SettingsWindow.cpp`, `CMakeLists.txt` | крючки `// fork:` (Init до LoadPreference, Tick перед NewFrame, кнопки/кружки в Appearance, +ThemeFiles.cpp) | JSON-темы | my/themes |
| новое: `src/app/ui/ThemeFiles.{h,cpp}`, `docs/themes.md`, `docs/theme-tokens.md` | загрузчик/горячая перезагрузка/экспорт | JSON-темы | my/themes |

## Правила
- Наш код — в **собственных файлах** (новые .cpp/.h), а не внутри файлов апстрима.
- В файлах апстрима — только короткие «крючки» (вызов/include) с комментарием `// fork:`.
- `AppShared.h` без крайней необходимости не трогаем (горячий файл, конфликты почти гарантированы).
- Каждая правка файла апстрима заносится в таблицу выше — по ней разрешаются конфликты при синхронизации.
