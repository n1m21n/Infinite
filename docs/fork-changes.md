# Реестр изменений форка в коде апстрима

| Файл | Что изменено | Зачем | Ветка |
|------|--------------|-------|-------|
| `src/app/panels/InspectorPanel.{h,cpp}` (новые) | Панель-инспектор, настройка, обёртки пинов `insp::*`, self-test `INFINITE_INSPECTORTEST` | Этап 3 `docs/plans/ux-roadmap.md` | `my/inspector` |
| `src/app/frame/StageNodeBodies.cpp` | Диспетчер `dynamic_cast → Draw*Params` вынесен в `DrawNodeParamsBody(GraphNode&)` (тело без изменений); вызов `InspectorFollowSelection()`; `InspectorOwnsParams(gn)` в условии register-only прохода | Один диспетчер на канвас и инспектор | `my/inspector` |
| `src/app/ui/ModWidgets.cpp`, `ui/ParamWidgets.cpp`, `ui/Theme.cpp`, `bodies/GeometryParams.cpp`, `bodies/SamplerBodies.cpp` | `ed::BeginPin/EndPin/PinPivotAlignment/PinRect` → `insp::*` (механическая замена) + include | Вне канваса `ed::BeginPin` нельзя: в инспекторе точка рисуется обычным ImGui | `my/inspector` |
| `src/core/Modulation.h`, `src/core/Palette.h` | `TruncateFrameParams(n)` / `TruncateFrameColors(n)` | Снять дубли регистраций полного прохода инспектора | `my/inspector` |
| `frame/StageLayout.cpp`, `StageSidePanels.cpp`, `StageFloating.cpp` | Док Left / Right / Floating инспектора | Раскладка | `my/inspector` |
| `frame/StageKeyboard.cpp`, `StageMenuBar.cpp` | Shift+I, пункт View → Show inspector, кнопка на рейке | Открытие панели | `my/inspector` |
| `frame/StageMenuBar.cpp` (верхняя панель) | Вызов `DrawTopBarParamsTools(centreY)` после кнопки аудио: плитки «показать все параметры», «скрыть все параметры» (подсветка = все открыты/все скрыты, в подсказке счётчик открытых) и переключатель инспектора; пункты меню Nodes остались | Быстрый доступ | `my/inspector` |
| `graph/PatchIO.cpp`, `panels/SettingsWindow.cpp` | Ключи `inspector*` в workspace-settings; строка «Node parameters» в Canvas & Workspace | Сохранение раскладки и настройки | `my/inspector` |
| `frame/StageTestsB.cpp`, `CMakeLists.txt` | Вызов self-test; новый .cpp в списке | Тест/сборка | `my/inspector` |

## Правила
- Наш код — в **собственных файлах** (новые .cpp/.h), а не внутри файлов апстрима.
- В файлах апстрима — только короткие «крючки» (вызов/include) с комментарием `// fork:`.
- `AppShared.h` без крайней необходимости не трогаем (горячий файл, конфликты почти гарантированы).
- Каждая правка файла апстрима заносится в таблицу выше — по ней разрешаются конфликты при синхронизации.
