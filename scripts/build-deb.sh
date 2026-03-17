#!/bin/bash
# Build DEB package for Ubuntu 24.04

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION=$(cat "$SCRIPT_DIR/DOCKER_VERSION" 2>/dev/null || echo "0.3.0")

cd "$PROJECT_ROOT"

echo "=========================================="
echo "Building RavBot DEB Package"
echo "Version: $VERSION"
echo "=========================================="

# Check if required tools are installed
if ! command -v dpkg-buildpackage &> /dev/null; then
    echo "Error: dpkg-buildpackage not found. Installing build dependencies..."
    sudo apt-get update
    sudo apt-get install -y dpkg-dev debhelper devscripts
fi

# Install build dependencies
echo ""
echo "Installing build dependencies..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libssl-dev \
    libcurl4-openssl-dev \
    nlohmann-json3-dev \
    libspdlog-dev \
    zlib1g-dev \
    libsqlite3-dev \
    nodejs \
    npm

# Clean previous builds
echo ""
echo "Cleaning previous builds..."
rm -rf debian/ravbot
rm -f ../ravbot_*.deb ../ravbot_*.changes ../ravbot_*.buildinfo

# Build the package
echo ""
echo "Building DEB package..."
dpkg-buildpackage -us -uc -b

# Move the package to dist/
echo ""
echo "Moving package to dist/..."
mkdir -p dist
mv ../ravbot_*.deb dist/
mv ../ravbot_*.changes dist/ 2>/dev/null || true
mv ../ravbot_*.buildinfo dist/ 2>/dev/null || true

# Generate checksums
echo ""
echo "Generating checksums..."
cd dist
for file in ravbot_*.deb; do
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
echo "Package location: dist/ravbot_${VERSION}-1_$(dpkg --print-architecture).deb"
echo ""
echo "To install:"
echo "  sudo dpkg -i dist/ravbot_${VERSION}-1_$(dpkg --print-architecture).deb"
echo "  sudo apt-get install -f  # Install missing dependencies"
echo ""
echo "To test installation:"
echo "  ravbot --version"
echo "  ravbot onboard"
echo ""
