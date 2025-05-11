#!/usr/bin/env bash

# monitor_smartbdev_stats.sh
# Continuously displays /proc/smartbdev/stats in-place, updating every INTERVAL seconds.

# Usage: ./monitor_smartbdev_stats.sh [INTERVAL]
# Default INTERVAL is 1 second

INTERVAL=${1:-1}
FILE="/proc/smartbdev/stats"

# Check readability
if [[ ! -r "$FILE" ]]; then
  echo "Error: Cannot read $FILE" >&2
  exit 1
fi

# Determine number of lines to move cursor up after each print
LINES=$(wc -l < "$FILE")

# Clear screen once
clear

while true; do
  # Print the stats
  cat "$FILE"
  
  # Wait
 #  sleep "$INTERVAL"
  
  # Move cursor up by $LINES to overwrite previous output
  printf "\033[${LINES}A"
done
