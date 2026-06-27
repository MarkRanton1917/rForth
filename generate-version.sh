#!/bin/bash
VERSION=$(git describe --tags --abbrev=7 2>/dev/null || git rev-parse --short HEAD 2>/dev/null)
echo "// Auto-generated version header" > "$1"
echo "#define GIT_VERSION \"$VERSION\"" >> "$1"
