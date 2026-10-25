#!/usr/bin/env bash
set -euo pipefail
printf 'Satr Studio - Install programming tools\n\n'
if ! command -v apt-get >/dev/null; then
    printf 'This installer supports Ubuntu and Debian-based systems.\n'
    exit 1
fi
packages=(g++ gcc clangd xterm python3 python3-venv python3-pip nodejs npm openjdk-21-jdk)
sudo apt-get update
sudo apt-get install -y "${packages[@]}"
python3 -c 'import ssl, sqlite3, json; print("Python ready")'
node -e 'console.log("JavaScript ready")'
printf '\nTools installed. You can now run your programs.\n'

javac -version
java -version
