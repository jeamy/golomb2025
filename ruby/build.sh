#!/bin/bash
# Build script for Ruby Golomb Ruler Finder

# Make scripts executable
chmod +x golomb.rb

# Create symbolic link in bin directory
mkdir -p ../bin
rm -f ../bin/golomb-ruby
ln -s "$(pwd)/golomb.rb" ../bin/golomb-ruby

echo "Ruby Golomb Ruler Finder is ready!"
echo "You can run it with: ./golomb.rb <marks> [options]"
echo "or from anywhere: $(pwd)/../bin/golomb-ruby <marks> [options]"
