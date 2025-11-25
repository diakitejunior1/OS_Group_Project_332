#!/bin/bash
set -euo pipefail
PG="./processgroup"

echo "Starting test: error cases"

# Build if not exists
gcc -std=c11 -Wall -Wextra -O2 -o processgroup processgroup.c

# Remove pidfile to simulate first run
rm -f /tmp/processgroup.pids || true

# Try add invalid PID
echo "Adding invalid PID (abc):"
! $PG -a abc || echo "ERROR: expected failure"

# Try add non-existing PID (very large)
echo "Adding non-existing PID 999999:"
$PG -a 999999 || echo "Expected: error message printed."

# Corrupt pid file with a bad line and then list
echo "Writing malformed pid file and testing read robustness"
printf "not_a_pid\n1234\n-5\n" > /tmp/processgroup.pids
$PG -l

# Test concurrent writers: spawn two add operations simultaneously to check locking
(sleep 0.1; $PG -a $$) &  # add this script's PID
(sleep 0.1; $PG -a $$) &  # concurrent add
wait

$PG -l

echo "Error cases test done."
