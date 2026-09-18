#!/usr/bin/env bash
# shellcheck shell=bash

###############################################################################
# Global configuration
###############################################################################

set -Eeuo pipefail

###############################################################################
# Repository
###############################################################################

CONFIG_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

readonly REPO_ROOT="$(cd "${CONFIG_DIR}/.." && pwd)"

readonly KERNEL_REPO="${REPO_ROOT}"

###############################################################################
# Toolchain
###############################################################################

readonly TOOLCHAIN_DIR="${REPO_ROOT}/toolchains/gcc-14.2.0-nolibc/aarch64-linux"
readonly TOOLCHAIN_BIN="${TOOLCHAIN_DIR}/bin"

export PATH="${TOOLCHAIN_BIN}:${PATH}"

export ARCH=arm64
export SUBARCH=arm64

###############################################################################
# Output directories
###############################################################################

readonly OUT_DIR="${REPO_ROOT}/out"
readonly DIST_DIR="${REPO_ROOT}/dist"

###############################################################################
# Targets & variants
###############################################################################

readonly TARGETS=(
    gs201
    zuma
    zumapro
)

readonly VARIANTS=(
    ksu-susfs
    ksu-susfs-vpnhide
    ksu-next-susfs
    ksu-next-susfs-vpnhide
)

###############################################################################
# Device configuration
###############################################################################

declare -Ar DEFCONFIGS=(
    [gs201]="gs201_defconfig"
    [zuma]="zuma_defconfig"
    [zumapro]="zumapro_defconfig"
)

###############################################################################
# REPOS
###############################################################################

readonly KSU_REPO="https://github.com/tiann/KernelSU"
readonly KSU_BRANCH="main"

readonly KSU_NEXT_REPO="https://github.com/KernelSU-Next/KernelSU-Next"
readonly KSU_NEXT_BRANCH="dev"

readonly SUSFS_REPO="https://gitlab.com/simonpunk/susfs4ksu.git"
readonly SUSFS_BRANCH="gki-android14-6.1"

readonly ANYKERNEL_REPO="https://github.com/Ante0/AnyKernel3"
readonly ANYKERNEL_BRANCH_PREFIX="sultan-17"

readonly PATCHES_REPO="https://github.com/Ante0/kernel_patches"
readonly PATCHES2_REPO="https://github.com/Ante0/fix_patches"

readonly NOMOUNT_REPO="https://github.com/maxsteeel/nomount"
readonly NOMOUNT_BRANCH="dev"

readonly VPNHIDE_REPO="https://github.com/okhsunrog/vpnhide"
readonly VPNHIDE_BRANCH="main"

###############################################################################
# Release
###############################################################################

readonly RELEASE_PREFIX="SULTAN17"

###############################################################################
# Logging
###############################################################################

msg() {
    printf '\n\033[1;32m==>\033[0m %s\n' "$*"
}

warn() {
    printf '\n\033[1;33m==>\033[0m %s\n' "$*" >&2
}

die() {
    printf '\n\033[1;31mERROR:\033[0m %s\n' "$*" >&2
    exit 1
}

###############################################################################
# Validation
###############################################################################

require_target() {
    local target="$1"

    [[ " ${TARGETS[*]} " == *" ${target} "* ]] \
        || die "Unknown target: ${target}"
}

require_variant() {
    local variant="$1"

    [[ " ${VARIANTS[*]} " == *" ${variant} "* ]] \
        || die "Unknown varian: ${variant}"
}

###############################################################################
# Helpers
###############################################################################

defconfig_for_target() {
    local target="$1"

    require_target "$target"

    printf '%s\n' "${DEFCONFIGS[$target]}"
}

build_config_for_target() {
    local target="$1"

    require_target "$target"

    printf '%s\n' "${BUILD_CONFIGS[$target]}"
}

build_name() {
    local target="$1"
    local variant="$2"

    printf '%s_%s_%s.zip\n' \
        "${RELEASE_PREFIX}" \
        "${target}" \
        "${variant}"
}

ensure_directories() {
    mkdir -p \
        "${OUT_DIR}" \
        "${DIST_DIR}"
}
