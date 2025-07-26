#!/bin/bash

# Validate args
if [ $# -ne 2 ]; then
    echo "Usage: $0 <number> <start_register>"
    exit 1
fi

INPUT="$1"
START_REG="$2"

if [[ "$INPUT" == *.* ]]; then
    HEX_REP=$(python3 -c "import struct; print(struct.pack('>f', float($INPUT)).hex())")
else
    HEX_REP=$(printf "%08X" "$INPUT")
fi

if [[ ${#HEX_REP} -ne 8 ]]; then
    echo "Error: Unexpected hexadecimal length for float."
    exit 1
fi

HEX_PART1=${HEX_REP:0:4}
HEX_PART2=${HEX_REP:4:4}

echo "\"$START_REG\": \"0x$HEX_PART1\", \"$((START_REG + 1))\": \"0x$HEX_PART2\","
