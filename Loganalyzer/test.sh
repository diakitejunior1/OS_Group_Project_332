#!/bin/bash

# ===========================
#   LOGANALYZER TEST SCRIPT
# ===========================

echo "🔧 Compiling loganalyzer.c ..."
gcc -o loganalyzer loganalyzer.c -lpthread

if [ $? -ne 0 ]; then
    echo "❌ Compilation failed."
    exit 1
fi

echo "✅ Compilation successful!"
echo ""

# ======= Configuration ========
LOGFILE="huge.log"
KEYWORD="ERROR"
THREADS=4
# ===============================

# Check log file exists
if [ ! -f "$LOGFILE" ]; then
    echo "❌ Log file '$LOGFILE' not found."
    echo "Create it or update LOGFILE variable."
    exit 1
fi

echo "📄 Using log file: $LOGFILE"
echo ""

# ===========================
#   RUN TEST CASES
# ===========================

echo "===== TEST 1: Show memory map ====="
./loganalyzer -f "$LOGFILE" -m
echo ""

echo "===== TEST 2: Count errors ====="
./loganalyzer -f "$LOGFILE" -e
echo ""

echo "===== TEST 3: Keyword search ====="
./loganalyzer -f "$LOGFILE" -k "$KEYWORD"
echo ""

echo "===== TEST 4: Multithreaded keyword search ====="
./loganalyzer -f "$LOGFILE" -k "$KEYWORD" -t $THREADS
echo ""

echo "===== TEST 5: Stats ====="
./loganalyzer -f "$LOGFILE" -s
echo ""

echo "🎉 ALL TESTS COMPLETED"
