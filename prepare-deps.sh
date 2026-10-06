#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
stage="$(mktemp -d)"
trap 'rm -rf -- "$stage"' EXIT
mkdir -p .deps/boost .deps/json/include/nlohmann
curl -fLSs --connect-timeout 15 --max-time 120 https://archive.ubuntu.com/ubuntu/pool/main/b/boost1.74/libboost1.74-dev_1.74.0-14ubuntu3_amd64.deb -o "$stage/boost.deb"
printf '%s  %s\n' 4d9c90e43f0d25db6280d1ee326771cbb76462f73b9430f06bac1de8d05b7a78 "$stage/boost.deb" | sha256sum -c -
dpkg-deb --extract "$stage/boost.deb" .deps/boost
curl -fLSs --connect-timeout 15 --max-time 120 https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp -o "$stage/json.hpp"
printf '%s  %s\n' 9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6 "$stage/json.hpp" | sha256sum -c -
cp "$stage/json.hpp" .deps/json/include/nlohmann/json.hpp
curl -fLSs --connect-timeout 15 --max-time 120 https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT -o "$stage/json-license.txt"
printf '%s  %s\n' 86b998c792894ccb911a1cb7994f7a9652894e7a094c0b5e45be2f553f45cf14 "$stage/json-license.txt" | sha256sum -c -
cp "$stage/json-license.txt" .deps/json/LICENSE.MIT
printf "Dependencies ready: Boost1.74.0 and nlohmann/json3.11.3\n"
