#!/bin/sh
# Сборка пакета BeAE_Plus-<версия>-1-<архитектура>.hpkg (запускать на Haiku).
#
#   package/build_hpkg.sh            собрать программу и плагины, затем пакет
#   package/build_hpkg.sh --no-build пакет из уже собранных файлов (BeAE/BeAE и plugins/*/*.so)
#
# Что окажется внутри пакета (после установки это /system, то есть /boot/system):
#   apps/BeAE-Plus/BeAE                         программа
#   apps/BeAE-Plus/{Bitmaps,Help,Languages,Samples}  её ресурсы (программа ищет их рядом с собой)
#   lib/ladspa/<Группа>/*.so                    плагины LADSPA (хост BeAE ищет их в /system/lib/ladspa)
#   data/deskbar/menu/Applications/BeAE-Plus    ярлык в меню «Приложения» (Deskbar)
#
# Установка: положить .hpkg в ~/config/packages (для себя) или /system/packages (для всех)
# либо дважды щёлкнуть по нему и выбрать установку. Удаление - удалить файл пакета.
set -e

# Программа собирается под ту Haiku, где её собрали, и может не запуститься на более старой.
# Чтобы пакет не ставился на неё, укажите версию, например: HAIKU_REQ=" >= r1~beta5_hrev58590-1" package/build_hpkg.sh
HAIKU_REQ="${HAIKU_REQ:-}"
VERSION="3.1"
REVISION="1"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
ARCH="$(getarch 2>/dev/null || uname -m)"
[ "$ARCH" = "x86_64" ] || [ "$ARCH" = "x86_gcc2" ] || [ "$ARCH" = "x86" ] || ARCH="x86_64"
OUT="$ROOT/BeAE_Plus-$VERSION-$REVISION-$ARCH.hpkg"
STAGE="$ROOT/package/stage"

if [ "$1" != "--no-build" ]; then
	echo "== Сборка программы"
	( cd "$ROOT/Core-src" && make )
	echo "== Сборка плагинов"
	( cd "$ROOT/plugins" && make )
fi

[ -x "$ROOT/BeAE/BeAE" ] || { echo "Нет $ROOT/BeAE/BeAE: сначала соберите программу (make в Core-src)" >&2; exit 1; }
command -v package >/dev/null 2>&1 || { echo "Нет команды package (есть только на Haiku)" >&2; exit 1; }

echo "== Подготовка файлов"
rm -rf "$STAGE"
APPDIR="$STAGE/apps/BeAE-Plus"
mkdir -p "$APPDIR" "$STAGE/lib/ladspa" "$STAGE/data/deskbar/menu/Applications" "$STAGE/documentation/packages/beae_plus"

cp "$ROOT/BeAE/BeAE" "$APPDIR/"
for d in Bitmaps Help Languages Samples; do
	[ -d "$ROOT/BeAE/$d" ] && cp -R "$ROOT/BeAE/$d" "$APPDIR/"
done
cp "$ROOT/BeAE/license.txt" "$APPDIR/" 2>/dev/null || true

# плагины: каждая подпапка plugins/<имя>/ ставится в lib/ladspa/<Группа>/ (правило make install)
( cd "$ROOT/plugins" && make install DEST="$STAGE/lib/ladspa" )
NPLUG=0
for f in "$STAGE"/lib/ladspa/*/*.so; do
	[ -f "$f" ] && NPLUG=$((NPLUG + 1))
done
[ "$NPLUG" -gt 0 ] || { echo "Плагины не найдены: выполните make в папке plugins" >&2; exit 1; }
echo "   плагинов: $NPLUG"

# ярлык в «Приложения»: относительная ссылка на программу
ln -s ../../../../apps/BeAE-Plus/BeAE "$STAGE/data/deskbar/menu/Applications/BeAE-Plus"

cp "$ROOT/README.md" "$ROOT/BeAE/license.txt" "$ROOT/screenshot.png" "$STAGE/documentation/packages/beae_plus/" 2>/dev/null || true

cat > "$STAGE/.PackageInfo" <<EOF
name			beae_plus
version			$VERSION-$REVISION
architecture		$ARCH
summary			"Multitrack audio editor with automation and LADSPA plug-ins"
description		"BeAE-Plus is a multitrack audio editor for Haiku (a fork of BeAE by Frans van Nispen).

* Multitrack editing with clips, effects, groups and mixdown
* Volume and pan automation curves, master automation
* USB gamepad control and live automation recording
* Recording with latency compensation, MIDI import, tuner, analyzers
* LADSPA plug-ins: reverbs, equalizers, compressors, gate, overdrive, autotune and more

Saving to MP3, Ogg Vorbis, FLAC and Opus uses the external encoders lame, oggenc, flac and opusenc if they are installed."
packager		"Stas Korobov <grossekatze1@gmail.com>"
vendor			"BeAE-Plus"
licenses {
	"BSD (3-clause)"
}
copyrights {
	"2000-2003 Xentronix Software, Frans van Nispen"
	"2026 Stas Korobov"
}
provides {
	beae_plus = $VERSION
	app:BeAE_Plus = $VERSION
}
requires {
	haiku$HAIKU_REQ
}
urls {
	"https://github.com/grossekatzeru/BeAE-Plus"
}
EOF

echo "== Создание пакета"
rm -f "$OUT"
( cd "$STAGE" && package create -C "$STAGE" "$OUT" )
echo "Готово: $OUT"
echo "Проверка содержимого:  package list \"$OUT\""
