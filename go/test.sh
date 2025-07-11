#!/bin/bash

# test.sh - Test script for Go Golomb Ruler Finder

set -e

echo "Testing Go Golomb Ruler Finder..."

# Ensure we're in the correct directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Build if needed
if [ ! -f "../bin/golomb-go" ]; then
    echo "Building..."
    ./build.sh
fi

echo ""
echo "Test 1: Basic functionality (5 marks)"
./golomb 5 -b

echo ""
echo "Test 2: Multi-processing (8 marks)"
./golomb 8 -mp -b

echo ""
echo "Test 3: Verbose output (6 marks)"
./golomb 6 -v -b

echo ""
echo "Test 4: Help message"
./golomb -help

echo ""
echo "Test 5: Verify LUT data consistency"
echo "Checking known optimal rulers..."

# Test a few known optimal rulers
test_cases=(
    "4:6:[0,1,4,6]"
    "5:11:[0,1,4,9,11]"
    "6:17:[0,1,4,10,12,17]"
    "7:25:[0,1,4,10,18,23,25]"
)

for test_case in "${test_cases[@]}"; do
    IFS=':' read -r marks expected_length expected_positions <<< "$test_case"
    echo "Testing $marks marks (expected length $expected_length)..."
    
    # Run the solver
    result=$(./golomb "$marks" -b 2>/dev/null | grep "Found ruler:")
    
    if [[ $result == *"$expected_positions"* ]]; then
        echo "✅ $marks marks: PASS"
    else
        echo "❌ $marks marks: FAIL - got $result"
    fi
done

echo ""
echo "All tests completed!"
echo ""
echo "Output files are in the 'out/' directory"
echo "Compare with C implementation using:"
echo "  diff ../out/GOL_n5_b.txt out/GOL_n5_b.txt"
