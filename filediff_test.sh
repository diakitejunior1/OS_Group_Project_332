#!/bin/bash

echo "Test Cases for filediffadvanced"

mkdir -p tests

# Create minimal test files
echo "A1 line1" > tests/a1.txt
echo "A1 line1 modified" > tests/a2.txt
echo -n "" > tests/empty
echo -n -e "\x01\x02\x03" > tests/bin1
echo -n -e "\x01\x02\xFF" > tests/bin2

run_test() {
    echo ""
    echo ">>> $1"
    echo "Command: $2"
    eval $2
}

# Text tests
run_test "Same text file" "./filediffadvanced text tests/a1.txt tests/a1.txt"
run_test "Different text file" "./filediffadvanced text tests/a1.txt tests/a2.txt"

# Binary tests
run_test "Same binary file" "./filediffadvanced binary tests/bin1 tests/bin1"
run_test "Different binary file" "./filediffadvanced binary tests/bin1 tests/bin2"

# Error tests
run_test "File not found" "./filediffadvanced text no_such_file tests/a1.txt"

echo ""