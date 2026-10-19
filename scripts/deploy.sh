#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

git pull --ff-only
git submodule update --init --recursive

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

PREFIX="${PREFIX:-/usr/local}"
if [ -w "$PREFIX/bin" ]; then
  cmake --install build --prefix "$PREFIX"
else
  sudo cmake --install build --prefix "$PREFIX"
fi

echo "installed: $PREFIX/bin/mini_terminal"
