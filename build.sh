#!/usr/bin/env bash

set -eu

info() {
	echo -e "\033[33m$1\033[m"
}

die() {
	echo -e "\033[31m$1\033[m"
	exit 1
}

build_type="Release"

if test $# -ge 1; then
    case "$1" in
        "debug")
            build_type="Debug"
            ;;
        "clean")
            mkdir -p ./out
            rm -rf ./out xpp
            exit 0
            ;;
        *)
            die "Unrecognised option '$1'"
            ;;
    esac
fi

mkdir -p ./out
cd out || die "cd error"
cmake -DCMAKE_BUILD_TYPE="$build_type" .. -GNinja
ninja
