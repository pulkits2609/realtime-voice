
#!/usr/bin/env bash

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

# Validate all arguments before performing any work.
BUILD_TYPE=Debug
EXPLICIT_MODE=""
CLEAN_BUILD=false
usage() {
    echo "Usage: ./build.sh [-debug | -release] [-clean]" >&2
    echo "Default: Debug. Flags may be entered in any order." >&2
    exit 1
}
for argument in "$@"; do
    case "$argument" in
        -clean) CLEAN_BUILD=true ;;
        -debug|-release)
            mode=Debug
            [[ "$argument" != -release ]] || mode=Release
            if [[ -n "$EXPLICIT_MODE" && "$EXPLICIT_MODE" != "$mode" ]]; then
                echo "ERROR: -debug and -release cannot be used together." >&2
                usage
            fi
            EXPLICIT_MODE="$mode"
            BUILD_TYPE="$mode"
            ;;
        *) usage ;;
    esac
done

# Clean only when explicitly requested
if "$CLEAN_BUILD"; then
    [[ "$BUILD_DIR" == "$PROJECT_DIR/build" && ! -L "$BUILD_DIR" ]] || {
        echo "ERROR: Unsafe build directory." >&2
        exit 1
    }
    echo "Cleaning previous build..."
    rm -rf -- "$BUILD_DIR"
fi

echo "Configuring Ubuntu $BUILD_TYPE build..."

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "Building project..."

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo "Build successful!"
echo "Output: $BUILD_DIR"
