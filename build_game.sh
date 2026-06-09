#!/bin/bash
# Build script for The Binding of Isaac 3DS

echo "=== Building The Binding of Isaac for 3DS ==="
echo ""

# Method 1: Using Docker (recommended if you don't have devkitPRO installed locally)
if command -v docker &> /dev/null; then
    echo "Building with Docker..."
    docker run --rm -v "$(pwd):/project" devkitpro/devkitarm bash -c "cd /project && make clean && make"
    exit $?
fi

# Method 2: Using local devkitPRO installation
if [ -d "/opt/devkitpro" ]; then
    echo "Building with local devkitPRO..."
    export DEVKITPRO=/opt/devkitpro
    export DEVKITARM=$DEVKITPRO/devkitARM
    export PATH=$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH
    make clean && make
    exit $?
fi

echo "Error: Neither Docker nor devkitPRO installation found."
echo ""
echo "Install devkitPRO from: https://devkitpro.org/wiki/Getting_Started"
echo "OR"
echo "Install Docker from: https://docs.docker.com/get-docker/"
exit 1
