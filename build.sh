
#!/usr/bin/env bash

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

# Validate arguments
if [[ $# -gt 1 ]] || { [[ $# -eq 1 ]] && [[ "$1" != "-clean" ]]; }; then
    echo "Usage: ./build.sh [-clean]"
    exit 1
fi

# Clean only when explicitly requested
if [[ "${1:-}" == "-clean" ]]; then
    echo "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

echo "Configuring Ubuntu Release build..."

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release

echo "Building project..."

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo "Build successful!"
echo "Output: $BUILD_DIR"
