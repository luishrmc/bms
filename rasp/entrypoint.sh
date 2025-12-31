#!/bin/bash

set -eu

if [ ! -x /opt/bms/bms ]; then
  echo "ERROR: /opt/bms/bms not found or not executable."
  echo "Make sure you copied it into rpi_deploy/bin/bms and ran: chmod +x bin/bms"
  exit 1
fi

exec /opt/bms/bms "$@"
