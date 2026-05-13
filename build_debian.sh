#!/usr/bin/env bash
# build_debian.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="$(cat "${SCRIPT_DIR}/VERSION")"
PKG_NAME="betterconn"
BUILD_DIR="${SCRIPT_DIR}/build"
DIST_DIR="${SCRIPT_DIR}/dist"
ARCH="$(dpkg --print-architecture 2>/dev/null || uname -m | sed 's/x86_64/amd64/;s/aarch64/arm64/')"
STAGING_DIR="${BUILD_DIR}/.deb_staging/${PKG_NAME}_${VERSION}_${ARCH}"

if ! command -v dpkg-deb &>/dev/null; then
    echo "[!!] dpkg-deb not found. Install it with: sudo apt install dpkg"
    exit 1
fi

rm -rf "${BUILD_DIR}"
mkdir -p "${DIST_DIR}"

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j"$(nproc)"

mkdir -p "${STAGING_DIR}/usr/bin"
mkdir -p "${STAGING_DIR}/DEBIAN"

cp "${BUILD_DIR}/cli/betterconn" "${STAGING_DIR}/usr/bin/betterconn"
chmod 755 "${STAGING_DIR}/usr/bin/betterconn"

cat > "${STAGING_DIR}/DEBIAN/control" <<CONTROL
Package: ${PKG_NAME}
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: Christian <pusheandoando@github>
Section: net
Priority: optional
Depends: iptables, iproute2, kmod, iputils-ping, curl, ethtool, iw
Description: betterconn - Linux network optimizer for Debian
 Maximizes internet connection quality on Debian-based systems.
 Features adaptive real-time tuning with WiFi priority support.
CONTROL

dpkg-deb --root-owner-group --build "${STAGING_DIR}" "${DIST_DIR}/${PKG_NAME}_${VERSION}.deb"
rm -rf "${BUILD_DIR}"

echo "[OK] package ready  ->  dist/${PKG_NAME}_${VERSION}.deb  (${VERSION})"
echo "[OK] install with:  sudo apt install ./dist/${PKG_NAME}_${VERSION}.deb"