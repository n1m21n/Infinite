#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends \
  build-essential \
  clang \
  lld \
  cmake \
  ninja-build \
  pkg-config \
  git \
  ccache \
  curl \
  patchelf \
  python3 \
  libx11-dev \
  libxrandr-dev \
  libxinerama-dev \
  libxcursor-dev \
  libxi-dev \
  libxkbcommon-dev \
  libwayland-dev \
  wayland-protocols \
  libgl-dev \
  libegl-dev \
  mesa-utils \
  libgl1-mesa-dri \
  libfontconfig-dev \
  libfreetype-dev \
  libasound2-dev \
  xvfb \
  xauth \
  ffmpeg \
  pulseaudio \
  pulseaudio-utils \
  alsa-utils \
  weston \
  kmod \
  file \
  imagemagick \
  xdotool \
  ca-certificates \
  fonts-dejavu-core

rm -rf /var/lib/apt/lists/*
