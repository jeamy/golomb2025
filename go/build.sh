#!/bin/bash

# build.sh - Build script for Go Golomb Ruler Finder

set -e

echo "Building Go Golomb Ruler Finder..."

# Ensure we're in the correct directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Create bin directory if it doesn't exist
mkdir -p ../bin

# Build the Go binary
go build -o ../bin/golomb-go .

echo "Build complete: ../bin/golomb-go"

# Create symlink for convenience
if [ ! -L "golomb" ]; then
    ln -sf ../bin/golomb-go golomb
    echo "Created symlink: golomb -> ../bin/golomb-go"
fi

echo "Usage: ./golomb <marks> [options]"
echo "       ../bin/golomb-go <marks> [options]"
