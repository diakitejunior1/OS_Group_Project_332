#!/bin/bash

mode=$1  # text / binary / error / all

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

# 1. TEXT MODE
run_text_tests() {
    echo ""
    echo "===== TEXT MODE TESTS ====="
    run_test "Same text file" "./filediffadvanced text tests/a1.txt tests/a1.txt"
    run_test "Different text file" "./filediffadvanced text tests/a1.txt tests/a2.txt"
}

# 2. BINARY MODE 
run_binary_tests() {
    echo ""
    echo "===== BINARY MODE TESTS ====="
    run_test "Same binary file" "./filediffadvanced binary tests/bin1 tests/bin1"
    run_test "Different binary file" "./filediffadvanced binary tests/bin1 tests/bin2"
}


# 3. ERROR HANDLING 
run_error_tests() {
    echo ""
    echo "===== ERROR HANDLING TESTS ====="
    run_test "File not found" "./filediffadvanced text no_such_file tests/a1.txt"
}

# MODE CONTROL
if [ "$mode" == "text" ]; then
    run_text_tests
elif [ "$mode" == "binary" ]; then
    run_binary_tests
elif [ "$mode" == "error" ]; then
    run_error_tests
else
    run_text_tests
    run_binary_tests
    run_error_tests
    echo ""
    echo "===== ALL TESTS COMPLETE ====="
fi