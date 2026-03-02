#!/bin/sh

set -e

EXECUTABLE="$1"
if [ -z "$EXECUTABLE" ]; then
  EXECUTABLE="./newserv"
fi

TMP_CONFIG="$(mktemp)"
TMP_OUTPUT="$(mktemp)"
ACCOUNT_ID="DEADBEEF"
ACCOUNT_FILE="system/licenses/${ACCOUNT_ID}.json"

cleanup() {
  rm -f "$TMP_CONFIG" "$TMP_OUTPUT" "$ACCOUNT_FILE"
}
trap cleanup EXIT

sed 's/"AllowSavingAccounts": false,/"AllowSavingAccounts": true,\n  "RunInteractiveShell": true,/' tests/config.json > "$TMP_CONFIG"

printf "add-account id=%s\nreload accounts\nlist-accounts\ndelete-account %s\nexit\n" "$ACCOUNT_ID" "$ACCOUNT_ID" | \
  "$EXECUTABLE" --config="$TMP_CONFIG" > "$TMP_OUTPUT" 2>&1

rg -q "Account ${ACCOUNT_ID} added" "$TMP_OUTPUT"
rg -q "Account: [0-9]+/${ACCOUNT_ID}" "$TMP_OUTPUT"
rg -q "Account deleted" "$TMP_OUTPUT"
