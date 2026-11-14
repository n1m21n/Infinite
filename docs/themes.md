# Пользовательские темы (JSON)

Тема — один файл `*.json` в папке `<SettingsDir>/themes/` (на macOS: `~/Library/Application Support/Infinite/themes/`;
открыть: Settings → Appearance → «Open themes folder»). Файл виден в списке Preset после 15 встроенных тем.
Правка файла подхватывается сама (папка опрашивается раз в ~0.5 с); «Reload» — принудительно.

## Минимальная тема
Любые ключи можно опускать — недостающее берётся из встроенной темы `base` (по умолчанию «Infinite Ember»).
```json
{
  "name": "Мой закат",
  "base": "Infinite Ember",
  "colors": { "windowBg": "#14111A", "panelBg": "#1C1824", "text": "#EDE6F2",
              "textDim": "#9A90A8", "border": "#3A3347", "accent": "#FF8A4C" }
}
```

## Ключи
| Секция | Содержимое |
|---|---|
| `name` | Имя в списке. Нет — имя файла. Совпало с чужим — добавится « (2)». |
| `base` | Имя **встроенной** темы-основы (как в списке Preset). Неизвестное → первая встроенная, предупреждение. |
| `colors` | 6 базовых цветов: `windowBg panelBg text textDim border accent`. Из них выводятся ~61 цвет ImGui. Светлая/тёмная определяется яркостью `windowBg`. Текст автоматически подтягивается до контраста 4.5:1. |
| `categories` | Цвета категорий нод: `Source 3D Compositing Effects Modulators Prediction Macros Utility Notes Synths AudioEffects`. |
| `cables` | Цвета кабелей: `Stream Modulation Audio Note Palette`. |
| `tokens` | Переопределение именованных ролей (71 шт., полный список — [theme-tokens.md](theme-tokens.md)). Значение: `"#RRGGBB[AA]"` (и тёмной, и светлой) или `{"dark": "...", "light": "..."}`. |
| `_что-угодно` | Ключи, начинающиеся с `_`, игнорируются (комментарии). |

Цвет — `#RRGGBB` или `#RRGGBBAA`. Ключи секций `colors/categories/cables/tokens` — **точные** имена (регистр важен).

## Ошибки
- Битый JSON (нет запятой, лишняя скобка): тема **не** ломается — остаётся прошлая версия файла (или тема пропускается, если файл новый); в Settings под кнопками красным: `файл: JSON error near line N: …`.
- Неизвестный ключ / плохой цвет: предупреждение, остальное применяется.

## Образец со всеми ключами
Settings → Appearance → **Create from current**: пишет `themes/My <имя>.json` со всеми ключами текущей темы (в том числе всеми 71 токенами) и выбирает его. Это работает и для встроенных тем.

## Settings → Appearance
Для пользовательской темы появляется ряд из 6 кружков базовых цветов: правка сразу пишется в файл (остальные ключи сохраняются).

## Что пока НЕ покрывает тема
Около 600 безымянных цветовых литералов в коде (`tok::pal::c_…`, `IM_COL32`) ещё не переведены в роли — они темой не меняются (этап 4в плана `docs/plans/ux-roadmap.md`). Совместимость: `Infinite.theme` (выбранное имя) и `Infinite.appearance` (ручные правки категорий/кабелей) работают как раньше, поверх темы.

## Для разработчика
Цветовые роли в `tokens.json` теперь `inline`-переменные (не `constexpr`) + дефолты `d_<имя>` + реестр `tok::kColorTokens`; вызовы `tok::U32/V4` не менялись. `python3 tools/design/build_tokens.py` генерирует `Tokens.gen.h` и `docs/theme-tokens.md` (`--check` проверяет оба). Код: `src/app/ui/ThemeFiles.{h,cpp}`, `src/core/CategoryColors.cpp` (`SetUserPresets`).
