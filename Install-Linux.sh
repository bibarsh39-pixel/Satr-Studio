#!/usr/bin/env bash
set -euo pipefail
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    echo 'This installer requires 64-bit Intel/AMD Linux.' >&2; exit 1
fi
if ! command -v apt-get >/dev/null || ! command -v dpkg >/dev/null; then
    echo 'Use Ubuntu 24.04 or newer, Linux Mint 22, or a compatible Debian-based distribution.' >&2
    exit 1
fi
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
package="$here/Satr-Studio-0.9.0-Linux-amd64.deb"
if [ ! -f "$package" ]; then echo 'Keep this installer beside the Satr Studio .deb file.' >&2; exit 1; fi
echo 'Satr Studio — Linux installation'
echo 'Installs C, C++, Python, JavaScript and Java tools, the terminal, and missing system libraries.'
echo 'Internet is needed for missing system packages. Your code and settings are kept.'
echo
if [ "${1:-}" != '--yes' ]; then
    read -r -p 'Continue? [y/N] ' answer
    case "$answer" in y|Y|yes|YES) ;; *) exit 0 ;; esac
fi
# Use a readable temporary location even when the download directory is private.
temporary=$(mktemp -d /tmp/satr-install.XXXXXXXX)
trap 'rm -f -- "$temporary/package.deb"; rmdir -- "$temporary"' EXIT
cp -- "$package" "$temporary/package.deb"
chmod 755 "$temporary"; chmod 644 "$temporary/package.deb"
admin=()
if [ "$(id -u)" -ne 0 ]; then
    if ! command -v sudo >/dev/null; then echo 'Run this installer from an administrator terminal.' >&2; exit 1; fi
    admin=(sudo)
fi
"${admin[@]}" apt-get update
"${admin[@]}" apt-get install --reinstall -y "$temporary/package.deb"
echo
echo 'Installed. Open Satr Studio from the applications menu.'
echo 'To remove the app later: sudo apt remove satr-studio'
