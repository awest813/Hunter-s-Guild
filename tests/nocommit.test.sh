#!/bin/sh

set -e

rg -n NOCOMMIT src/ || exit 0
echo "Failed: one or more NOCOMMIT comments were found"
exit 1
