#!/bin/bash

echo "==> Resetting source tree..."
git reset --hard HEAD
git clean -ffd
