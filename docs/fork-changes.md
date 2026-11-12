# Реестр изменений форка в коде апстрима

| Файл | Что изменено | Зачем | Ветка |
|------|--------------|-------|-------|
| `src/core/OscMessage.{h,cpp}` | `DecodePacket` (bundle, рекурсивно), `MatchAddress` (`*`,`?`), теги `T/F/d/h` в `DecodeMany`; `Decode` теперь через `DecodeMany` | Bitwig/DrivenByMoss шлёт bundle'ами | my/osc |
| `src/nodes/OscNodes.{h,cpp}` | OSC Receive переведён на общий приёмник порта; параметр `argIndex`, wildcard, OSC learn, `BindError()` | несколько нод на порт, learn, индикация ошибки | my/osc |
| `src/nodes/OscPortHub.{h,cpp}` (новые) + `CMakeLists.txt` (+1 строка) | один сокет/поток на порт, буфер 64 КБ, раздача по подписчикам | | my/osc |
| `src/app/bodies/ParamBodies1.cpp` | `DrawOscReceiveParams`: arg index, кнопка OSC Learn, строка ошибки, диапазон low/high 0..127 | UI | my/osc |
| `src/app/selftest/MiscTests.cpp` | в `RunOscControlTest` добавлены проверки OSC Receive (bundle, общий порт, wildcard, learn, bind-ошибка) + includes | self-test | my/osc |

## Правила
- Наш код — в **собственных файлах** (новые .cpp/.h), а не внутри файлов апстрима.
- В файлах апстрима — только короткие «крючки» (вызов/include) с комментарием `// fork:`.
- `AppShared.h` без крайней необходимости не трогаем (горячий файл, конфликты почти гарантированы).
- Каждая правка файла апстрима заносится в таблицу выше — по ней разрешаются конфликты при синхронизации.
