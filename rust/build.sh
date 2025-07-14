#!/bin/bash
# Build-Script für die Rust-Implementation des Golomb-Ruler-Finders

# Stelle sicher, dass wir im richtigen Verzeichnis sind
cd "$(dirname "$0")"

# Erstelle das Ausgabeverzeichnis, falls es nicht existiert
mkdir -p out

# Baue das Rust-Projekt
cargo build --release

# Erstelle einen symbolischen Link im bin-Verzeichnis, falls es existiert
if [[ -d ../bin ]]; then
  echo "Creating symlink in ../bin directory"
  ln -sf "$(pwd)/target/release/golomb" "../bin/golomb-rust"
fi

# Berechtigungen setzen
chmod +x target/release/golomb

echo "Build completed successfully!"
echo "Run with: ./target/release/golomb <marks> [-v] [-b] [-mp]"
