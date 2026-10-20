#!/bin/sh
#
# Install pfSense-pkg-speedtest on pfSense, outside of the official repository.
#
#   fetch -o - https://raw.githubusercontent.com/bdelphin/pfSense-pkg-speedtest/HEAD/install.sh | sh
#
# or, from a copy of the repository on the firewall:  sh install.sh
# Uninstall with:  sh install.sh uninstall   (or System > Package Manager)
#
# The package is built on the firewall from the port in net/pfSense-pkg-speedtest
# with "pkg create", then installed with "pkg add", exactly like a repository package.

set -e

REPO=${SPEEDTEST_REPO:-bdelphin/pfSense-pkg-speedtest}
REF=${SPEEDTEST_REF:-HEAD}
NAME=pfSense-pkg-speedtest
PORT_PATH=net/${NAME}

die() {
	echo "Error: $*" >&2
	exit 1
}

[ "$(id -u)" = "0" ] || die "run this script as root."
[ -f /etc/rc.packages ] || die "this is not a pfSense system."

if [ "$1" = "uninstall" ]; then
	pkg info -e ${NAME} || die "${NAME} is not installed."
	pkg delete -y ${NAME}
	echo "${NAME} removed."
	exit 0
fi

WORK=$(mktemp -d /tmp/${NAME}.XXXXXX)
trap 'rm -rf "${WORK}"' EXIT

# Port sources: next to this script, or downloaded from GitHub
SRC=""
case "$0" in
*/*) [ -d "$(dirname "$0")/${PORT_PATH}" ] && SRC="$(cd "$(dirname "$0")" && pwd)" ;;
*) [ -d "./${PORT_PATH}" ] && [ -f "./install.sh" ] && SRC="$(pwd)" ;;
esac
if [ -z "${SRC}" ]; then
	echo ">>> Downloading ${REPO} (${REF})..."
	mkdir "${WORK}/src"
	fetch -q -o "${WORK}/src.tar.gz" "https://codeload.github.com/${REPO}/tar.gz/${REF}" ||
		die "download failed."
	tar -xzf "${WORK}/src.tar.gz" -C "${WORK}/src" --strip-components=1
	SRC="${WORK}/src"
fi
PORT="${SRC}/${PORT_PATH}"
[ -f "${PORT}/Makefile" ] || die "port not found in ${SRC}."

VERSION=$(sed -n 's/^PORTVERSION=[[:space:]]*//p' "${PORT}/Makefile")
REVISION=$(sed -n 's/^PORTREVISION=[[:space:]]*//p' "${PORT}/Makefile")
if [ -n "${REVISION}" ] && [ "${REVISION}" != "0" ]; then
	VERSION="${VERSION}_${REVISION}"
fi
DATADIR=/usr/local/share/${NAME}
STAGE="${WORK}/stage"
PLIST="${WORK}/plist"
: > "${PLIST}"

echo ">>> Building ${NAME}-${VERSION}..."
grep -v '^@' "${PORT}/pkg-plist" | while read -r entry; do
	case "${entry}" in
	%%DATADIR%%/*) path="${DATADIR}/${entry#%%DATADIR%%/}" ;;
	/*) path="${entry}" ;;
	*) path="/usr/local/${entry}" ;;
	esac
	src="${PORT}/files${path}"
	[ -f "${src}" ] || die "missing file ${src}."
	mkdir -p "${STAGE}$(dirname "${path}")"
	case "${path}" in
	/usr/local/bin/*) install -m 0755 "${src}" "${STAGE}${path}" ;;
	*) install -m 0644 "${src}" "${STAGE}${path}" ;;
	esac
	echo "${path}" >> "${PLIST}"
done
sed -i '' -e "s|%%PKGVERSION%%|${VERSION}|" "${STAGE}${DATADIR}/info.xml"

ABI=$(pkg config abi | cut -d: -f1,2):*
COMMENT=$(sed -n 's/^COMMENT=[[:space:]]*//p' "${PORT}/Makefile")
MAINTAINER=$(sed -n 's/^MAINTAINER=[[:space:]]*//p' "${PORT}/Makefile")
RC="/usr/local/bin/php -f /etc/rc.packages ${NAME}"

cat > "${WORK}/+MANIFEST" <<MANIFEST
name: "${NAME}"
version: "${VERSION}"
origin: "${PORT_PATH}"
comment: "${COMMENT}"
maintainer: "${MAINTAINER}"
www: "https://github.com/${REPO}"
abi: "${ABI}"
prefix: "/usr/local"
licenselogic: "single"
licenses: ["APACHE20"]
categories: ["net"]
desc: <<EOD
$(cat "${PORT}/pkg-descr")
EOD
scripts: {
  post-install: "${RC} POST-INSTALL"
  pre-deinstall: "${RC} DEINSTALL"
  post-deinstall: "${RC} POST-DEINSTALL"
}
MANIFEST

mkdir "${WORK}/out"
pkg create -M "${WORK}/+MANIFEST" -p "${PLIST}" -r "${STAGE}" -o "${WORK}/out"
PKGFILE=$(ls "${WORK}"/out/${NAME}-${VERSION}.* | head -1)
[ -f "${PKGFILE}" ] || die "package build failed."

if pkg info -e ${NAME}; then
	echo ">>> Removing the installed version..."
	pkg delete -y ${NAME}
fi

echo ">>> Installing ${NAME}-${VERSION}..."
pkg add "${PKGFILE}"

# System > Package Manager only lists packages installed from the product repository
PRODUCT=$(/usr/local/bin/php -r 'require_once("/etc/inc/globals.inc"); echo g_get("product_name");' 2>/dev/null)
PRODUCT=${PRODUCT:-pfSense}
pkg annotate -q -y -D ${NAME} repository > /dev/null 2>&1 || true
pkg annotate -q -y -A ${NAME} repository "${PRODUCT}"

echo ""
echo "${NAME} ${VERSION} installed: Diagnostics > Speedtest, or 'pfspeedtest' from the shell."
