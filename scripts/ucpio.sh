#!/bin/bash -e

echo "$1/tools/ucpio/ucpio -o < $2 > $3"
"$1"/tools/ucpio/ucpio -o < "$2" > "$3"

