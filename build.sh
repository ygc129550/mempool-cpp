#!/bin/bash
set -e

case "${1:-build}" in
    build)
        cmake -S . -B build
        cmake --build build
        ;;
    clean)
        rm -rf build
        echo "build directory removed."
        ;;
    rebuild)
        rm -rf build
        cmake -S . -B build
        cmake --build build
        ;;
    *)
        echo "Usage: $0 {build|clean|rebuild}"
        exit 1
        ;;
esac
