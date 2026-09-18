#!/usr/bin/env bash
# shellcheck shell=bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=config.sh
source "${SCRIPT_DIR}/config.sh"

###############################################################################
# Helpers
###############################################################################

github_commit_info() {
    local repo="$1"
    local branch="$2"
    local path="${3:-}"

    repo="$(repo_path "$repo")"

    gh api \
        "repos/${repo}/commits?sha=${branch}&path=${path}&per_page=1" \
        --jq '.[0] | [.sha, (.commit.message | split("\n")[0])] | @tsv'
}

gitlab_commit_info() {
    local repo="$1"
    local branch="$2"

    repo="$(repo_path "$repo")"
    repo="${repo//\//%2F}"

    curl -fsSL \
        "https://gitlab.com/api/v4/projects/${repo}/repository/branches/${branch}" |
        jq -r '[.commit.id, .commit.title] | @tsv'
}

repo_path() {
    local repo="$1"

    repo="${repo#https://}"
    repo="${repo#*/}"
    repo="${repo%.git}"

    printf '%s\n' "$repo"
}

###############################################################################
# Read previous nightly.json
###############################################################################

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

NOTES_FILE="${TMP}/release_notes.md"

cat >"$NOTES_FILE" <<EOF
## Automated Nightly Build

This build was triggered by upstream changes.


EOF

JSON_FILE="${TMP}/nightly.json"

LOCAL_JSON="${SCRIPT_DIR}/nightly.json"

if [[ -f "$LOCAL_JSON" ]]; then
    JSON_FILE="$LOCAL_JSON"
fi

if gh release view Nightly >/dev/null 2>&1; then

    ASSET_URL=$(
        gh api repos/${GITHUB_REPOSITORY}/releases/tags/Nightly \
            --jq '.assets[]
                | select(.name=="nightly.json")
                | .url'
    )

    if [[ -n "${ASSET_URL:-}" ]]; then
        gh api \
            -H "Accept: application/octet-stream" \
            "${ASSET_URL}" > "${JSON_FILE}"
    fi
fi

###############################################################################
# Current SHAs
###############################################################################

IFS=$'\t' read -r NEW_KSU KSU_MSG \
    < <(github_commit_info "$KSU_REPO" "$KSU_BRANCH" "kernel")

IFS=$'\t' read -r NEW_NEXT NEXT_MSG \
    < <(github_commit_info "$KSU_NEXT_REPO" "$KSU_NEXT_BRANCH" "kernel")

IFS=$'\t' read -r NEW_SUSFS SUSFS_MSG \
    < <(gitlab_commit_info "$SUSFS_REPO" "$SUSFS_BRANCH")

IFS=$'\t' read -r NEW_NOMOUNT NOMOUNT_MSG \
    < <(github_commit_info "$NOMOUNT_REPO" "$NOMOUNT_BRANCH" "kernel")

IFS=$'\t' read -r NEW_VPNHIDE VPNHIDE_MSG \
    < <(github_commit_info "$VPNHIDE_REPO" "$VPNHIDE_BRANCH" "kpatch")

NEW_SOURCE="$(git rev-parse HEAD)"
SOURCE_MSG="$(git log -1 --pretty=%s)"

###############################################################################
# First run
###############################################################################

if [[ ! -f "${JSON_FILE}" ]]; then

    msg "First nightly run detected."

    BUILD_VARIANTS=(
        	ksu-susfs
		ksu-susfs-vpnhide
		ksu-next-susfs
		ksu-next-susfs-vpnhide
    )

else

    OLD_SOURCE="$(jq -r '.source // ""' "${JSON_FILE}")"
    OLD_KSU="$(jq -r '.kernelsu' "${JSON_FILE}")"
    OLD_NEXT="$(jq -r '.kernelsu_next' "${JSON_FILE}")"
    OLD_SUSFS="$(jq -r '.susfs' "${JSON_FILE}")"
    OLD_NOMOUNT="$(jq -r '.nomount' "${JSON_FILE}")"
    OLD_VPNHIDE="$(jq -r '.vpnhide' "${JSON_FILE}")"

    BUILD_VARIANTS=()

### SOURCE
if [[ "${OLD_SOURCE}" != "${NEW_SOURCE}" ]]; then
    BUILD_VARIANTS+=(
        	ksu-susfs
        	ksu-next-susfs
		ksu-susfs-vpnhide
		ksu-next-susfs-vpnhide
    )

    SHORT_SHA="${NEW_SOURCE:0:7}"

    cat >>"$NOTES_FILE" <<EOF
### SULTAN17 SOURCE

- ${SHORT_SHA} — ${SOURCE_MSG}

EOF
    fi

##KERNELSU
    if [[ "${OLD_KSU}" != "${NEW_KSU}" ]]; then
        BUILD_VARIANTS+=(ksu-susfs)

        SHORT_SHA="${NEW_KSU:0:7}"

cat >>"$NOTES_FILE" <<EOF
###KernelSU

- ${SHORT_SHA} — ${KSU_MSG}

EOF
    fi

### KERNELSU-NEXT
    if [[ "${OLD_NEXT}" != "${NEW_NEXT}" ]]; then
        BUILD_VARIANTS+=(ksu-next-susfs)

	SHORT_SHA="${NEW_NEXT:0:7}"

cat >>"$NOTES_FILE" <<EOF
###KernelSU-Next

- ${SHORT_SHA} — ${NEXT_MSG}

EOF
    fi

###SUSFS
    if [[ "${OLD_SUSFS}" != "${NEW_SUSFS}" ]]; then
        BUILD_VARIANTS+=(ksu-susfs ksu-next-susfs)

	SHORT_SHA="${NEW_SUSFS:0:7}"

cat >>"$NOTES_FILE" <<EOF
###SusFS

- ${SHORT_SHA} — ${SUSFS_MSG}

EOF
    fi

    if [[ "${OLD_NOMOUNT}" != "${NEW_NOMOUNT}" ]]; then
        BUILD_VARIANTS+=(ksu-susfs ksu-next-susfs ksu-susfs-vpnhide ksu-next-susfs-vpnhide)

	SHORT_SHA="${NEW_NOMOUNT:0:7}"

cat >>"$NOTES_FILE" <<EOF
###NoMount

- ${SHORT_SHA} — ${NOMOUNT_MSG}

EOF
    fi

    if [[ "${OLD_VPNHIDE}" != "${NEW_VPNHIDE}" ]]; then
        BUILD_VARIANTS+=(ksu-susfs-vpnhide ksu-next-susfs-vpnhide)

        SHORT_SHA="${NEW_VPNHIDE:0:7}"

cat >>"$NOTES_FILE" <<EOF
###VPNHide

- ${SHORT_SHA} — ${NOMOUNT_MSG}

EOF
    fi

if ((${#BUILD_VARIANTS[@]} > 0)); then
    mapfile -t BUILD_VARIANTS < <(
        printf "%s\n" "${BUILD_VARIANTS[@]}" | sort -u
    )
fi

fi

if ((${#BUILD_VARIANTS[@]} == 0)); then
    msg "No upstream changes detected."
else
    msg "Detected changes for: ${BUILD_VARIANTS[*]}"
fi

###############################################################################
# Generate matrix
###############################################################################

if ((${#BUILD_VARIANTS[@]} == 0)); then
    JSON='{"include":[]}'
else
    JSON='{"include":['
    FIRST=true

    for BRANCH in 17.0.0-cp2a 17.0.0-cp3a; do
    for TARGET in "${TARGETS[@]}"; do
        for VARIANT in "${BUILD_VARIANTS[@]}"; do

            if $FIRST; then
                FIRST=false
            else
                JSON+=","
            fi

            JSON+="{\"branch\":\"${BRANCH}\",\"target\":\"${TARGET}\",\"variant\":\"${VARIANT}\"}"

            done
        done
    done

    JSON+="]}"
fi

###############################################################################
# Outputs
###############################################################################

{
    echo "matrix=$JSON"
    echo "source_sha=$NEW_SOURCE"
    echo "ksu_sha=$NEW_KSU"
    echo "next_sha=$NEW_NEXT"
    echo "susfs_sha=$NEW_SUSFS"
    echo "nomount_sha=$NEW_NOMOUNT"
    echo "vpnhide_sha=$NEW_VPNHIDE"
    echo "release_notes<<EOF"
    cat "$NOTES_FILE"
    echo "EOF"
} >> "$GITHUB_OUTPUT"

msg "Generated matrix"

echo "$JSON"
