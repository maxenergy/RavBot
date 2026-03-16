#!/bin/bash
# Test DEB package structure and metadata

set -e

if [ $# -eq 0 ]; then
    echo "Usage: $0 <path-to-deb-file>"
    exit 1
fi

DEB_FILE="$1"

if [ ! -f "$DEB_FILE" ]; then
    echo "Error: File not found: $DEB_FILE"
    exit 1
fi

echo "=========================================="
echo "Testing DEB Package: $(basename "$DEB_FILE")"
echo "=========================================="

# 1. Check package info
echo ""
echo "1. Package Information:"
dpkg-deb --info "$DEB_FILE"

# 2. List contents
echo ""
echo "2. Package Contents:"
dpkg-deb --contents "$DEB_FILE" | head -20
echo "..."
echo "Total files: $(dpkg-deb --contents "$DEB_FILE" | wc -l)"

# 3. Check control files
echo ""
echo "3. Control Files:"
dpkg-deb --control "$DEB_FILE" /tmp/quantclaw-control-$$
ls -la /tmp/quantclaw-control-$$
echo ""
echo "postinst script:"
cat /tmp/quantclaw-control-$$/postinst | head -20
rm -rf /tmp/quantclaw-control-$$

# 4. Check dependencies
echo ""
echo "4. Dependencies:"
dpkg-deb --field "$DEB_FILE" Depends

# 5. Check file size
echo ""
echo "5. Package Size:"
ls -lh "$DEB_FILE"

# 6. Verify package integrity
echo ""
echo "6. Package Integrity:"
dpkg-deb --validate "$DEB_FILE" && echo "✓ Package is valid" || echo "✗ Package validation failed"

# 7. Check for required files
echo ""
echo "7. Required Files Check:"
REQUIRED_FILES=(
    "./usr/bin/quantclaw"
    "./lib/systemd/system/quantclaw.service"
    "./usr/share/quantclaw/skills"
    "./usr/share/quantclaw/sidecar"
)

for file in "${REQUIRED_FILES[@]}"; do
    if dpkg-deb --contents "$DEB_FILE" | grep -q "$file"; then
        echo "✓ Found: $file"
    else
        echo "✗ Missing: $file"
    fi
done

echo ""
echo "=========================================="
echo "Test completed!"
echo "=========================================="
