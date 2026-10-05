#!/bin/sh
# Fetches the CC0 KayKit asset packs (by Kay Lousberg, www.kaylousberg.com)
# used by the asset pipeline into third_party/kaykit at pinned revisions.
set -e
cd "$(dirname "$0")/.."
mkdir -p third_party/kaykit
fetch() {
  name=$1; rev=$2
  dir=third_party/kaykit/$name
  if [ ! -d "$dir/.git" ]; then
    git clone --depth 1 "https://github.com/KayKit-Game-Assets/$name.git" "$dir"
  fi
  if [ "$(git -C "$dir" rev-parse HEAD)" != "$rev" ]; then
    git -C "$dir" fetch --depth 1 origin "$rev" && git -C "$dir" checkout -q "$rev"
  fi
  echo "ok $name @ $rev"
}
fetch KayKit-Character-Pack-Adventures-1.0 672074b73ba276876a19e8816ecdc5241817ab47
fetch KayKit-Character-Pack-Skeletons-1.0   15b62b9bad122f72926c10fb14d622c73819fa54
fetch KayKit-Dungeon-Remastered-1.0         b0ca9bd96a8072ab36a3a5464f00ed1e06a16d07
fetch KayKit-Halloween-Bits-1.0             6dc69bf6b2fa766a985754f35ec6a0324090e6c6
fetch KayKit-Medieval-Hexagon-Pack-1.0      84fa4e91af6a88989be7c99e0891cede11f2ca38
fetch KayKit-Furniture-Bits-1.0             96d5930a8dbdb363409bbc2d3341718b00e17c9c
