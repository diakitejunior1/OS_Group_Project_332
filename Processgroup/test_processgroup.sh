#!/bin/bash
set -euo pipefail
PG="./processgroup"

# Build
gcc -std=c11 -Wall -Wextra -O2 -o processgroup processgroup.c

echo "Starting test: normal flow"

# Start two dummy long-running processes (sleep) in background
sleep 300 &
PID1=$!
sleep 300 &
PID2=$!

echo "Dummy PIDs: $PID1 $PID2"

# Cleanup any previous pidfile
rm -f /tmp/processgroup.pids || true

# Add
$PG -a $PID1
$PG -a $PID2

# List should show both
echo "List output:"
$PG -l

# Show resources (reads /proc)
$PG -r

# Send SIGSTOP (19) to pause them
$PG -s 19

sleep 1

# Send SIGCONT (18)
$PG -s 18

# Kill them all
$PG -k

# Wait a bit for processes to exit and then cleanup dead entries:
sleep 1
$PG -c
$PG -l

echo "Normal flow test completed."

# Ensure background processes are gone (if still present, kill)
kill -0 $PID1 2>/dev/null && kill -9 $PID1 || true
kill -0 $PID2 2>/dev/null && kill -9 $PID2 || true
