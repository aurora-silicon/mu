#!/bin/sh
# SPDX-License-Identifier: MIT
# Compatibility entry point for existing J414s build automation.
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$script_dir/build-windows-native.sh" j414s "$@"
