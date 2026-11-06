# Плагины BeAE-Plus

Самостоятельные плагины LADSPA. Приложение от них не зависит; их может использовать любой хост LADSPA.

(Шумодав встроен в BeAE, меню Transform → «Шумодав»: он работает над выделением, а не в цепочке дорожки.)

| Файл | Название | Что делает |
|---|---|---|
| `spring_reverb.so` | Ламповое эхо (пружинный реверб) | имитация пружинного ревербератора, 3–5 пружин |

## Сборка и установка (Haiku)

    cd plugins
    make
    make install                      # ~/config/non-packaged/lib/ladspa
    make install DEST=/путь/BeAE/plugins    # папка plugins рядом с BeAE (подпапки допустимы)

BeAE ищет плагины в `plugins` рядом с программой (вместе с подпапками, глубина до 3, подпапка = подменю),
а также в стандартных папках LADSPA (`~/config/lib/ladspa`, `~/config/non-packaged/lib/ladspa`,
`/boot/system/lib/ladspa`, `/boot/system/non-packaged/lib/ladspa`, переменная `LADSPA_PATH`).

## Особенности

- Оба плагина стерео (2 входа, 2 выхода).
- Плагин стерео (2 входа, 2 выхода), без задержки.
- Названия портов на русском (UTF-8).
- Значения по умолчанию заданы подсказками LADSPA (HINT_DEFAULT_*), поэтому одинаковы в любом хосте.

## Проверка на Linux

    tests/spring/run_tests.sh
