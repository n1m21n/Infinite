#!/bin/bash
# Compiles Packet.swift + GoldenPacket.swift for macOS and checks the bytes against WatchPacket.cpp's output.
set -e
cd "$(dirname "$0")/.."
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
cp Sources/Packet.swift Sources/GoldenPacket.swift "$out/"
cp Tests/golden_main.swift "$out/main.swift"
swiftc -O "$out/Packet.swift" "$out/GoldenPacket.swift" "$out/main.swift" -o "$out/golden"
"$out/golden"
