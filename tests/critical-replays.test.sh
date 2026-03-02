#!/bin/sh

set -e

EXECUTABLE="$1"
if [ -z "$EXECUTABLE" ]; then
  EXECUTABLE="./newserv"
fi

CONFIG="tests/config.json"

# Non-BB login path coverage (legacy versions)
"$EXECUTABLE" --replay-log=tests/DC-11-2000-GameSmokeTest.test.txt --config="$CONFIG"
"$EXECUTABLE" --replay-log=tests/DC-NTE-GameSmokeTest.test.txt --config="$CONFIG"

# Lobby/game transition and quest interaction coverage
"$EXECUTABLE" --replay-log=tests/GC-HeartSymbol.test.txt --config="$CONFIG"
"$EXECUTABLE" --replay-log=tests/GCEp3-QuestDownload.test.txt --config="$CONFIG"

# Crossplay drop handling coverage
"$EXECUTABLE" --replay-log=tests/DCv1-DCv2-PCv2-CrossplayPrivateDrops.test.txt --config="$CONFIG"
