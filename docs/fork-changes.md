# Реестр изменений форка в коде апстрима

| Файл | Что изменено | Зачем | Ветка |
|------|--------------|-------|-------|
| _(пока пусто)_ | | | |
| src/app/Startup.cpp | `// fork:` экспорт NvOptimusEnablement/AmdPowerXpress (_WIN32) | дискретная GPU на ноутбуках | my/fps |
| src/core/GraphNode.h | `showMiniViewport` по умолчанию false | мини-вьюпорты дорогие | my/fps |
| src/core/NodeViewport.{h,cpp} | троттлинг 20 Гц, `LastColorTexture()` | FPS | my/fps |
| src/app/panels/ViewportPanel.cpp | пропуск невидимых, Preview quality | FPS | my/fps |
| src/nodes/{AnalyzeNodes,PaletteNode,PredictiveColoringNode}.{h,cpp} | `PboReadback` вместо glReadPixels | убрать sync-стоп | my/fps |
| src/app/panels/SettingsWindow.cpp, src/app/graph/PatchIO.cpp | настройка Preview quality + предупреждение GPU | диагностика | my/fps |
| src/app/frame/StageMenuBar.cpp, StagePresent.cpp | тултип GPU; `INFINITE_FPS_LOG` | диагностика | my/fps |
| новые: src/core/{PboReadback,PreviewQuality,GpuDiag}.h | наш код | | my/fps |

## Правила
- Наш код — в **собственных файлах** (новые .cpp/.h), а не внутри файлов апстрима.
- В файлах апстрима — только короткие «крючки» (вызов/include) с комментарием `// fork:`.
- `AppShared.h` без крайней необходимости не трогаем (горячий файл, конфликты почти гарантированы).
- Каждая правка файла апстрима заносится в таблицу выше — по ней разрешаются конфликты при синхронизации.
