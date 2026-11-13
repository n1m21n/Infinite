# 2026-11-13 — Инспектор параметров (ux-roadmap, этап 3), ветка `my/inspector`

## Что сделано
- `src/app/panels/InspectorPanel.{h,cpp}` — панель «Инспектор»: док справа (по умолчанию) / слева / плавающая; кнопка на рейке, View → Show inspector, Shift+I; состояние в workspace-settings (`inspectorOpen/Dock/W/Params`). Панель по умолчанию закрыта.
- Следует за выделением на канвасе, при мультивыделении — список нод, кнопка Pin.
- Один владелец отрисовки: глаз ноды закрыт → полный проход в инспекторе, на канвасе register-only проход; регистрации инспектора отрезаются (`TruncateFrameParams/Colors`), так что модуляция видит каждый параметр один раз при любом доке. Глаз открыт → в инспекторе подсказка + «Collapse and show here».
- Диспетчер параметров вынесен в `DrawNodeParamsBody(GraphNode&)` (`StageNodeBodies.cpp`). Пины модуляции в инспекторе — обёртки `insp::BeginPin…` (no-op вне канваса), ПКМ → существующее меню привязки, кабели идут на stub-пины свёрнутой ноды.
- Settings → Canvas & Workspace: «параметры в инспекторе» (по умолчанию) / «в ноде (classic)».
- Аудио-ноды не тронуты: инспектор показывает их тело целиком.
- Верхняя панель: плитки «Показать все параметры» / «Скрыть все параметры» (подсветка состояния, счётчик в подсказке) и переключатель инспектора; пункты меню Nodes оставлены.
- Self-test `INFINITE_INSPECTORTEST` (+`INFINITE_INSPECTORDOCK=2`, `INFINITE_INSPECTORKEEP`).

## Проверки
- INSPECTORTEST OK в правом и левом доке: blur (LFO→blur значение 4.36..10.00), Curves, Sphere (3D), LFO; при открытом глазе инспектор не рисует; дублей регистраций 0.
- `run-infinite-hygiene --fast`: 60/60.
- modulation-sweep (MODMATRIXTEST, MODMATRIXGEOM, MACROTEST, ROUNDTRIP…): 12/12.

## Что не получилось / осталось (backlog)
- Скриншоты `docs/screenshots/inspector-*.png` не сняты (нужен захват окна; `INFINITE_INSPECTORKEEP=1` оставляет окно открытым).
- Перетаскивание кабеля на строку инспектора — вне объёма.
- Русские переводы новых строк не добавлены (fallback на английский).
- Грабли: указатель из `SpawnNode` невалиден после следующего спавна — брать uid сразу.
- Сборка нового build-dev: FetchContent onnxruntime зависал; помогло `-DFETCHCONTENT_SOURCE_DIR_GLFW/ONNXRUNTIME_MAC_HEADERS` на `_deps` соседнего worktree.
