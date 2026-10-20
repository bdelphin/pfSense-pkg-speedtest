#!/bin/sh
# Preview Diagnostics > Speedtest on a development machine (PHP >= 8.1 with curl).
#
# Downloads the pfSense GUI sources (for the real CSS, JS and Form classes),
# copies the package with its absolute paths pointing to a temporary tree,
# and serves it on http://localhost:8080/. Tests run for real.
set -e

DEV_DIR=$(cd "$(dirname "$0")" && pwd)
FILES_DIR="$DEV_DIR/../net/pfSense-pkg-speedtest/files"
WORK=${WORK:-${TMPDIR:-/tmp}/pfspeedtest-dev}
PORT=${PORT:-8080}
mkdir -p "$WORK"

if [ ! -d "$WORK/pfsense/src/usr/local/www" ]; then
	echo "Downloading pfSense GUI sources..."
	mkdir -p "$WORK/pfsense"
	curl -sL https://codeload.github.com/pfsense/pfsense/tar.gz/refs/heads/master |
		tar -xzf - -C "$WORK/pfsense" --strip-components=1 pfsense-master/src/usr/local/www
fi

PKG_ROOT="$WORK/root"
rm -rf "$PKG_ROOT"
cp -R "$FILES_DIR" "$PKG_ROOT"
find "$PKG_ROOT" -type f \( -name '*.php' -o -name '*.inc' \) -exec \
	sed -i.orig -e "s|/usr/local/pkg/|$PKG_ROOT/usr/local/pkg/|g" -e "s|/usr/local/bin/php|$(command -v php)|g" {} +
find "$PKG_ROOT" -name '*.orig' -delete

export PFSENSE_WWW="$WORK/pfsense/src/usr/local/www"
export PKG_ROOT
export SPEEDTEST_RUN_DIR="$WORK/run" SPEEDTEST_DB_DIR="$WORK/db"
echo "http://localhost:$PORT/"
exec php -d include_path=".:$DEV_DIR/stubs" -S "localhost:$PORT" -t "$PFSENSE_WWW" "$DEV_DIR/router.php"
