# Реестр изменений форка в коде апстрима

| Файл | Что изменено | Зачем | Ветка |
|------|--------------|-------|-------|
| `src/app/ui/NodeTip.h` (новый), `ParamWidgets.cpp` | `app::NodeTip()` — обёртка диагностических подсказок: внутри ноды (`gInsideNodeCanvas`) паркует текст в `gNodeHoverTip`, вне — рисует сразу | прямые `SetTooltip/BeginTooltip` внутри `BeginNode…EndNode` ломали отрисовку ноды | my/quick-fixes |
| `ParamWidgets.cpp:EndParamDraw`, `design/components/{CodeBlock,FormParts,FieldWell,FilterBar,NodeHeader}.h`, `bodies/{ParamBodies2,AudioNodeBodies,GeometryParams}.cpp`, `frame/StageNodeBodies.cpp` | `ImGui::SetTooltip/BeginTooltip` → `app::NodeTip` (10 мест, исполняются внутри нод) | то же | my/quick-fixes |
| `frame/StageKeyboard.cpp` (~1015) | одна подсказка за кадр: scale-toggle → `gNodeHoverTip` → `gNodeHelpTip` (else-if) | два tooltip-окна в кадре перекрывали друг друга | my/quick-fixes |
| `frame/StagePopupsB.cpp` (меню "search") | при протягивании кабеля несовместимые ноды рисуются с alpha 0.3 (подменю категорий и текстовый поиск; в поиске совместимые первыми), остаются кликабельными; для кабеля от модулятора не применяется; множество совместимых имён — `unordered_set` (файловый static, пересобирается при смене drag-а); `AppShared.h` не тронут | этап 2 ux-roadmap | my/quick-fixes |
| `src/app/graph/PortProbeCache.{h,cpp}` (новые), `CMakeLists.txt` (+1 строка), `graph/Wiring.cpp:RecommendedNodeTypesForOutput`, `frame/StageLinks.cpp:RecommendedNodeTypesForPin`, `frame/StagePopupsB.cpp` (прогрев) | кэш портов по имени типа ноды: подсказки на drop кабеля считаются из кэша (маска принимаемых видов источника + виды выходов), без `MakeNode` на каждый drop; прогрев 3 мс/кадр, типы из расширений достраиваются при первом использовании; для param-пина проба только у модуляторов | drop кабеля стоил ~960 мс (Debug) | my/quick-fixes |

## Правила
- Наш код — в **собственных файлах** (новые .cpp/.h), а не внутри файлов апстрима.
- В файлах апстрима — только короткие «крючки» (вызов/include) с комментарием `// fork:`.
- `AppShared.h` без крайней необходимости не трогаем (горячий файл, конфликты почти гарантированы).
- Каждая правка файла апстрима заносится в таблицу выше — по ней разрешаются конфликты при синхронизации.
