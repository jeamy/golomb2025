#!/bin/bash

# golomb.sh - Script to run the Java Golomb Ruler Finder
# Usage: ./golomb.sh <marks> [options]

# Set Java home to Java 24 installation
JAVA_HOME=~/programming/java24
export JAVA_HOME

# Check if at least one argument is provided
if [ $# -lt 1 ]; then
    echo "Usage: $0 <marks> [options]"
    echo "Options:"
    echo "  -v         Enable verbose output"
    echo "  -mp        Use multi-processing solver"
    echo "  -b         Use best-known ruler length as starting point (added by default)"
    echo "  -o <file>  Write output to file"
    echo "  --help     Show this help message"
    exit 1
fi

# Check if the first argument is --help
if [ "$1" == "--help" ]; then
    echo "Golomb Ruler Finder (Java Implementation)"
    echo "========================================"
    echo ""
    echo "Usage: $0 <marks> [options]"
    echo ""
    echo "Arguments:"
    echo "  marks      Number of marks (required)"
    echo ""
    echo "Options:"
    echo "  -v         Enable verbose output"
    echo "  -mp        Use multi-processing solver"
    echo "  -b         Use best-known ruler length as starting point (added by default)"
    echo "  -o <file>  Write output to file"
    echo "  --help     Show this help message"
    exit 0
fi

# Ensure we're in the correct directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Create output directory if it doesn't exist
mkdir -p out

# Create bin directory and symlink if it doesn't exist
mkdir -p "$SCRIPT_DIR/../bin"
if [ ! -L "$SCRIPT_DIR/../bin/golomb-java" ]; then
    ln -sf "$SCRIPT_DIR/golomb.sh" "$SCRIPT_DIR/../bin/golomb-java"
    echo "Created symlink in ../bin/golomb-java"
fi

# Build the project if needed
if [ ! -f "target/classes/com/golomb/GolombMain.class" ]; then
    echo "Building project..."
    mvn clean compile
fi

# Run the Java application with all arguments
echo "Running Golomb Ruler Finder with $* arguments..."

# Check if -b flag is already present
if [[ ! " $* " =~ " -b " ]]; then
    # Add -b flag to use best-known ruler length as starting point
    $JAVA_HOME/bin/java --enable-preview -cp target/classes com.golomb.GolombMain "$@" -b
else
    $JAVA_HOME/bin/java --enable-preview -cp target/classes com.golomb.GolombMain "$@"
fi
