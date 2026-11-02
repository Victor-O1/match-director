#!/usr/bin/env bash
# Installs the system packages Match Director needs on Ubuntu 24.04 (WSL or native).
# Run once:  wsl -d Ubuntu -- bash scripts/install_deps_ubuntu.sh
set -euo pipefail

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build pkg-config git \
  clang clang-tidy clang-format \
  valgrind linux-tools-generic \
  ffmpeg libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  libegl-dev libgles-dev libegl-mesa0 libgl1-mesa-dri mesa-utils \
  fonts-dejavu-core gdb \
  libfreetype-dev libharfbuzz-dev \
  nlohmann-json3-dev libspdlog-dev \
  libgtest-dev libgmock-dev libbenchmark-dev

echo
echo "Installed versions:"
cmake --version | head -1
gcc --version | head -1
clang --version | head -1
pkg-config --modversion libavcodec libavformat egl glesv2 freetype2 || true
