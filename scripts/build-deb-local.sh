#!/bin/bash
# Build DEB package for Ubuntu 24.04 (without sudo)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION=$(cat "$SCRIPT_DIR/DOCKER_VERSION" 2>/dev/null || echo "0.3.0")

cd "$PROJECT_ROOT"

echo "=========================================="
echo "Building QuantClaw DEB Package"
echo "Version: $VERSION"
echo "=========================================="

# Check if required tools are installed
if ! command -v dpkg-buildpackage &> /dev/null; then
    echo "Error: dpkg-buildpackage not found."
    echo "Please install: sudo apt-get install dpkg-dev debhelper devscripts"
    exit 1
fi

# Check build dependencies
echo ""
echo "Checking build dependencies..."
MISSING_DEPS=""
for pkg in cmake g++ libssl-dev libcurl4-openssl-dev nlohmann-json3-dev libspdlog-dev zlib1g-dev libsqlite3-dev nodejs npm; do
    if ! dpkg -s "$pkg" &> /dev/null; then
        MISSING_DEPS="$MISSING_DEPS $pkg"
    fi
done

if [ -n "$MISSING_DEPS" ]; then
    echo "Missing dependencies:$MISSING_DEPS"
    echo "Please install: sudo apt-get install$MISSING_DEPS"
    exit 1
fi

# Clean previous builds
echo ""
echo "Cleaning previous builds..."
rm -rf debian/quantclaw
rm -f ../quantclaw_*.deb ../quantclaw_*.changes ../quantclaw_*.buildinfo ../quantclaw_*.tar.* 2>/dev/null || true

# Build the package
echo ""
echo "Building DEB package..."
dpkg-buildpackage -us -uc -b -j$(nproc)

# Move the package to dist/
echo ""
echo "Moving package to dist/..."
mkdir -p dist
mv ../quantclaw_*.deb dist/ 2>/dev/null || true
mv ../quantclaw_*.changes dist/ 2>/dev/null || true
mv ../quantclaw_*.buildinfo dist/ 2>/dev/null || true

# Generate checksums
echo ""
echo "Generating checksums..."
cd dist
for file in quantclaw_*.deb; do
    if [ -f "$file" ]; then
        sha256sum "$file" > "$file.sha256"
        echo "SHA256: $(cat "$file.sha256")"
    fi
done

echo ""
echo "=========================================="
echo "Build completed successfully!"
echo "=========================================="
echo ""
ls -lh quantclaw_*.deb
echo ""
echo "To install:"
echo "  sudo dpkg -i dist/quantclaw_${VERSION}-1_$(dpkg --print-architecture).deb"
echo "  sudo apt-get install -f  # Install missing dependencies if needed"
echo ""
echo "To test installation:"
echo "  quantclaw --version"
echo "  quantclaw onboard"
echo ""
