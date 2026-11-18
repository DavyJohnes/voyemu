# Voyemu

[English](https://github.com/voyemu/voyemu/blob/main/README.md) | Русский | [История изменений](https://github.com/voyemu/voyemu/blob/main/CHANGELOG.ru.md)

Voyemu — эмулятор автомобилей Voyah на базе Android (Free, Dreamer, Passion) для macOS, Windows и Linux. Он запускает оригинальную прошивку мультимедиа (Android, два экрана) и приборки (QNX), скачанную с автомобиля пользователя или взятую из файлов OTA-обновления; сама прошивка в Voyemu не входит. Voyemu эмулирует нажатия кнопок руля и ввод параметров автомобиля. Через Voyemu API скрипты получают снимки экрана и дерево элементов интерфейса, передают ввод и читают сообщения CAN.

Профиль прошивки описывает оригиналы и их адаптацию; подготовленная машина содержит гостевые образы, ресурсы и записываемые диски.

Voyemu работает на macOS 15+ с Apple Silicon, на Windows x64 и на Linux x64 с glibc 2.34+, системными libcurl и OpenSSL 3. Все перечисленные ниже профили поддержаны; приборка Passion экспериментальная.

[Установка](#установка) · [Быстрый старт](#быстрый-старт) · [Документация](#документация)

## Профили и возможности

| Профиль | Статус и состав |
| --- | --- |
| `free-09-chinese-20260106-6.0.3` | Free 6.0.3, поддержан. Оба экрана мультимедиа и приборка QNX, восстановление снимка RAM Android, USB-музыка, DVR, синтетический телефонный сценарий и штатная светлая/тёмная тема |
| `free-11-chinese-20260731-7.3.1` | Free 7.3.1, поддержан. Мультимедиа и приборка, темы Classic, Minimalist и Speedy Dash, состояния AEB, USB-музыка, DVR, Settings и синтетический телефонный сценарий. Штатная навигация требует активации |
| `passion-11-russian-20260403-2.1.4.80` | Passion, поддержана мультимедиа: оба штатных домашних экрана, синтетический телефонный сценарий, кнопки руля, USB-музыка и камера. Приборка QNX экспериментальная. В русской прошивке нет штатного приложения навигации |
| `free-11-chinese-20250710-6.11.1` | Free 6.11.1, поддержан. Мультимедиа и приборка; адаптер приборки поддерживает часть полей MCU и отвергает изменения остальных |
| `free-09-chinese-20221230-nxp-rc45` | Free NXP RC45, поддержан. Только мультимедиа; в профиле нет приборки. Штатная навигация требует активации |

[Страница каждого профиля](https://github.com/voyemu/voyemu/blob/main/docs/profiles/index.ru.md) описывает его гостей, оригиналы и особенности.

## Установка

Архивы релиза для macOS arm64 и Linux x64 (`.tar.xz`) и Windows x64 (`.zip`) лежат в [GitHub Releases](https://github.com/voyemu/voyemu/releases). На macOS и Linux последний стабильный релиз устанавливается через Homebrew командой `brew install voyemu/tap/voyemu`; см. [релиз](https://github.com/voyemu/voyemu/blob/main/docs/dev/release.ru.md).

## Быстрый старт

Команды выполняются из корня установки, см. [релиз](https://github.com/voyemu/voyemu/blob/main/docs/dev/release.ru.md#корень-установки); на Windows вместо `./voyemu` запускается `.\voyemu.exe` в PowerShell с теми же аргументами. `deps` устанавливает закреплённые инструменты, `fetch` импортирует архив прошивки, `build` подготавливает машину, а `start` открывает её окно.

```sh
./voyemu deps free-09-chinese-20260106-6.0.3
./voyemu fetch free-09-chinese-20260106-6.0.3 --source=/absolute/path/update.zip
./voyemu build free-09-chinese-20260106-6.0.3
./voyemu start machines/free-09-chinese-20260106-6.0.3
```

`status`, `stop` и `check` принимают тот же путь машины, а `./voyemu help` выводит полный синтаксис.

## Документация

- [Запуск и сеансы](https://github.com/voyemu/voyemu/blob/main/docs/how/launch.ru.md): команды жизненного цикла, подсистемы, параметры запуска и кадры QNX по gRPC.
- [Подготовка](https://github.com/voyemu/voyemu/blob/main/docs/how/preparation.ru.md): `deps`, `fetch` и `build`, а также способ импорта каждого [профиля](https://github.com/voyemu/voyemu/blob/main/docs/profiles/index.ru.md).
- [Профили и машины](https://github.com/voyemu/voyemu/blob/main/docs/how/profiles-machines.ru.md) и [корни `.local`](https://github.com/voyemu/voyemu/blob/main/docs/how/local-roots.ru.md): где что хранится, постоянные данные гостей и копирование машины.
- [Voyemu API](https://github.com/voyemu/voyemu/blob/main/docs/how/api.ru.md): запросы, владение, квитанции и справочник всех операций.
- [Сборка](https://github.com/voyemu/voyemu/blob/main/docs/dev/build.ru.md), [тесты](https://github.com/voyemu/voyemu/blob/main/docs/dev/tests.ru.md) и [CI](https://github.com/voyemu/voyemu/blob/main/docs/dev/ci.ru.md): сборка и проверка Voyemu из исходников.
- [Вся документация](https://github.com/voyemu/voyemu/blob/main/docs/index.ru.md).

Код Voyemu распространяется по [MIT](https://github.com/voyemu/voyemu/blob/main/LICENSE). Inter использует [SIL Open Font License](https://github.com/voyemu/voyemu/blob/main/profiles/common/fonts/Inter-OFL.txt). Сторонние код и инструменты сохраняют собственные лицензии; см. [уведомления о сторонних лицензиях](https://github.com/voyemu/voyemu/blob/main/src/licenses/README.md).

Штатные файлы прошивки и производные образы автомобиля исключены из публичных исходников. Рецепты с `guard` и проверяемые бинарные смещения описывают адаптацию; штатная графика извлекается в подготовленную машину. Лицензия проекта не даёт права распространять материалы OEM. Voyemu не связан с производителем автомобиля.
