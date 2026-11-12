# Сборка Infinite из исходников (macOS, Intel) — заметки

Дата: 2026-11-12. Коммит: `3207e41b7`. Исходный код не менялся.

## Машина и инструменты
- MacBook Pro, Intel Core i9-9980HK, 8 ядер / 16 потоков, 16 ГБ RAM, macOS 26.7.1 (Darwin 25.6.0), arch x86_64.
- Xcode CLT: `/Library/Developer/CommandLineTools`, Apple clang 21.0.0 (clang-2100.1.1.101).
- cmake 4.4.4 (`/usr/local/bin/cmake`), ninja есть (не использовался: ibuild работает через Makefiles), python 3.15.
- ccache: **не установлен** (не ставил — правило build-discipline требует спросить владельца). Следствие: пересборка/новые worktree компилируют всё заново.

## Важно: ibuild.py dev жёстко arm64, на Intel нужен x86_64
`scripts/ibuild.py` (строка 33) для режима dev задаёт `-DCMAKE_OSX_ARCHITECTURES=arm64`
(skill написан под M2 8 ГБ). Первая сборка получилась arm64 и на этом Mac **не запускается**
(`bad CPU type in executable`, exit 127; `lipo -archs` = arm64).
Обход (скрипты/исходники не правил) — переконфигурировать тот же каталог:

```bash
cmake -S . -B build-dev -DCMAKE_OSX_ARCHITECTURES=x86_64
scripts/ibuild.py build --mode dev
```
`ibuild` сохраняет кэш существующего каталога, поэтому x86_64 остаётся. Не запускать configure заново
с чистым `build-dev` через ibuild без этого флага — вернётся arm64. Проверка: `lipo -archs build-dev/Infinite.app/Contents/MacOS/Infinite` → `x86_64`.

## Команды (в порядке выполнения)
```bash
git submodule update --init external/vst3sdk
git -C external/vst3sdk submodule update --init pluginterfaces public.sdk   # VST3 включён, OFF не понадобился
scripts/ibuild.py status
scripts/ibuild.py build --mode dev            # 1-я сборка (arm64, непригодна)
cmake -S . -B build-dev -DCMAKE_OSX_ARCHITECTURES=x86_64
scripts/ibuild.py build --mode dev            # 2-я сборка (x86_64)
```
ibuild выбрал `-j4`, `nice 10`, лок `/tmp/infinite-build.lock`; лог `/tmp/ibuild_dev.log`.
configure скачивает зависимости через FetchContent (нужна сеть); ~25 с.

## Время
- Холодная сборка arm64 (configure + 596+ файлов, -j4): **418 с** (~7 мин).
- Пересборка под x86_64 после смены архитектуры (полная, 596 файлов): **391 с**.
- Каталог `build-dev`: ~1.2 ГБ. Предупреждений компилятора в логе: 23.

## Результат
- `.app`: `build-dev/Infinite.app` (бинарник `build-dev/Infinite.app/Contents/MacOS/Infinite`, Debug, x86_64), версия в заголовке окна: Infinite 1.2.0.
- Запуск: `INFINITE_NO_UPDATE_CHECK=1 build-dev/Infinite.app/Contents/MacOS/Infinite`. Окно открылось («Untitled — Infinite 1.2.0», 60.3 fps, пустой холст), скриншот: `docs/screenshots/first-run.png`. Процесс закрыт `pkill -x Infinite`.

## Пересборка одного файла
```bash
scripts/ibuild.py obj src/<path>/File.cpp     # только проверка синтаксиса, секунды
scripts/ibuild.py plan --mode dev             # сколько файлов устарело
scripts/ibuild.py build --mode dev            # инкрементально (пересобирает только изменённое)
```
Не вызывать голый `make -jN` / `cmake --build -jN`. Перед сборкой — `scripts/ibuild.py status`.

## Self-test (базовая линия до наших правок)
Команда (driver по умолчанию ищет `build/`, поэтому бинарник задан через `INFINITE_BIN`):
```bash
INFINITE_BIN=build-dev/Infinite.app/Contents/MacOS/Infinite \
  .claude/skills/run-infinite-hygiene/driver.sh --skip-build --fast
```
Итог: **passed 60, failed 0, xfail 0 — all checks green** (exit 0). Список упавших проверок пуст.
Полный вывод: `~/Library/Application Support/Claude/side-session-notes/local_73d67231-50b4-4b4e-bb63-41e38cd3665a/selftest-baseline-fast.log`.
Режимы `--full`/`--group` не запускались (не проверено).

## Проблемы и обходы
1. dev = arm64 на Intel → не запускается; обход выше (x86_64 через cmake).
2. Подмодуль vst3sdk не был загружен → загружен вручную (pluginterfaces, public.sdk); configure прошёл с VST3 ON.
3. `driver.sh` смотрит в `build/`, а ibuild dev строит в `build-dev/` → `INFINITE_BIN`.
4. ccache отсутствует; skill упоминает ручную установку из bottle (brew install там сломан) — не проверялось, не ставил.
5. Первый ibuild-запуск из-за обёртки фонового запуска не давал уведомления об окончании — ожидал через Monitor по строке `exit=` в логе.
6. cmake выдаёт предупреждение (policy CMP0135, DOWNLOAD_EXTRACT_TIMESTAMP, CMakeLists.txt:242) — безвредное.
