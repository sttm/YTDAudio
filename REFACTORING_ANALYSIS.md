# Отчет по рефакторингу кода

## Найденные проблемы

### 1. ДУБЛИКАТЫ КОДА

#### 1.1. Функция `normalizePath` дублируется в 3 местах:
- `src/app.cpp:70` - inline функция (полная версия с UNC поддержкой)
- `src/history/history_manager.cpp:11` - в анонимном namespace (полная версия)
- `src/window/window_manager.cpp:288` - лямбда-функция (упрощенная версия, без UNC)

**Проблема**: Разные реализации могут привести к несоответствиям, особенно на Windows.

**Решение**: Вынести в общий файл `src/common/path_utils.h/cpp`

---

### 2. DEAD CODE (Неиспользуемый код)

#### 2.1. `HistoryManager::rewriteHistoryFromTasks(const std::vector<std::unique_ptr<DownloadTask>>& tasks)`
- **Местоположение**: `src/history/history_manager.cpp:370`
- **Статус**: Placeholder, помечена как "(void)tasks; // Placeholder"
- **Использование**: НЕ используется, есть `App::rewriteHistoryFromTasks()` без параметров
- **Действие**: Удалить или реализовать

#### 2.2. `HistoryManager::addToHistory(DownloadTask* task)`
- **Местоположение**: `src/history/history_manager.cpp:376`
- **Статус**: Placeholder, помечена как "(void)task; // Placeholder"
- **Использование**: НЕ используется, есть `App::addToHistory(DownloadTask* task)`
- **Действие**: Удалить

#### 2.3. `HistoryManager::clearDeletedUrls()`
- **Местоположение**: `src/history/history_manager.cpp:352`
- **Статус**: Нужно проверить использование
- **Действие**: Проверить и удалить, если не используется

---

### 3. WINDOWS-СПЕЦИФИЧНЫЕ ПРОБЛЕМЫ

#### 3.1. Несогласованная обработка путей
- Разные версии `normalizePath` могут по-разному обрабатывать UNC пути (`\\server\share`)
- В `window_manager.cpp` упрощенная версия не обрабатывает UNC пути

#### 3.2. Множественные определения макросов
- `S_ISDIR`, `S_ISREG` определяются в нескольких местах
- `mkdir` макрос определяется в `app.cpp`

---

### 4. ДРУГИЕ ПРОБЛЕМЫ

#### 4.1. Дублирование логики создания HistoryItem
- ID генерируется в нескольких местах с одинаковой логикой
- Можно вынести в отдельную функцию

#### 4.2. Неиспользуемые includes
- Нужно проверить все `#include` на актуальность

---

## План рефакторинга

1. ✅ Создать `src/common/path_utils.h/cpp` с единой функцией `normalizePath`
2. ✅ Удалить дубликаты `normalizePath` из других файлов
3. ✅ Удалить dead code (placeholder функции)
4. ✅ Создать `src/common/history_utils.h/cpp` для генерации ID (убрать дубликаты)
5. ⚠️ Унифицировать Windows-специфичные макросы (S_ISDIR, S_ISREG, mkdir)

---

## Выполненные изменения

### ✅ 1. Унификация normalizePath
- Создан `src/common/path_utils.h/cpp` с функциями `normalizePath()` и `joinPath()`
- Удалены дубликаты из:
  - `src/app.cpp` (inline функция)
  - `src/history/history_manager.cpp` (анонимный namespace)
  - `src/window/window_manager.cpp` (лямбда-функция)
- Все вызовы заменены на `PathUtils::normalizePath()` и `PathUtils::joinPath()`

### ✅ 2. Удален dead code
- Удалена `HistoryManager::rewriteHistoryFromTasks(const std::vector<std::unique_ptr<DownloadTask>>& tasks)` - placeholder
- Удалена `HistoryManager::addToHistory(DownloadTask* task)` - placeholder
- Удалена `HistoryManager::clearDeletedUrls()` - не используется

### ✅ 3. Унификация генерации ID
- Создан `src/common/history_utils.h/cpp` с функциями:
  - `generateHistoryId()` - для новых элементов
  - `generateHistoryIdFromTimestamp()` - для обратной совместимости
- Удалены дубликаты генерации ID из 4 мест в `app.cpp`

### ✅ 4. Windows-специфичные макросы
- Создан `src/common/platform_macros.h` с общими макросами:
  - `mkdir` (compatibility)
  - `S_ISDIR`, `S_ISREG`, `S_IXUSR` (compatibility)
  - `popen`, `pclose` (compatibility)
- Заменены дубликаты в:
  - `src/app.cpp`
  - `src/platform/platform_utils.cpp`
  - `src/ui/ui_renderer.cpp`
  - `src/platform/path_finder.cpp`

---

## Итоги рефакторинга

### Удалено дублирующего кода:
- 3 реализации `normalizePath` (~120 строк)
- 4 дубликата генерации ID (~40 строк)
- 2 placeholder функции (dead code)
- 1 неиспользуемая функция `clearDeletedUrls`
- 4 дубликата Windows-макросов (~30 строк)

### Создано новых модулей:
- `src/common/path_utils.h/cpp` - унифицированная работа с путями
- `src/common/history_utils.h/cpp` - генерация ID для истории
- `src/common/platform_macros.h` - общие Windows-макросы

### Улучшения:
- ✅ Единая точка для нормализации путей (лучшая поддержка Windows UNC путей)
- ✅ Единая логика генерации ID (меньше ошибок)
- ✅ Меньше дубликатов = проще поддержка
- ✅ Улучшенная читаемость кода

