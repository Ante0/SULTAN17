#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

######################################################
# shellcheck source=config.sh
######################################################
source "${SCRIPT_DIR}/config.sh"

if [ $# -ne 2 ]; then
    echo "Usage: $0 <gs201|zuma|zumapro> <ksu-susfs|ksu-susfs-vpnhide|ksu-next-susfs|ksu-next-susfs-vpnhide"
    exit 1
fi

######################################################


######################################################
#SWITCHES
######################################################

case "$1" in
    gs201|zuma|zumapro)
        TARGET="$1"
        ;;
    *)
        echo "Error: '$1' is not a valid target."
	echo "This will select the correct defconfig"
        echo "Valid targets are: gs201, zuma, zumapro"
        exit 1
        ;;
esac

case "$2" in
    ksu-susfs|ksu-susfs-vpnhide|ksu-next-susfs|ksu-next-susfs-vpnhide)
        VARIANT="$2"
        ;;
    *)
        echo "Error: '$2' is not a valid variant."
        echo "Usage: $0 <gs201|zuma|zumapro> <ksu-susfs|ksu-susfs-vpnhide|ksu-next-susfs|ksu-next-susfs-vpnhide"
        exit 1
        ;;
esac

######################################################
######################################################

######################################################
# HELPERS
######################################################

clone_anykernel() {
	msg "Cloning AnyKernel"

	git clone \
		--depth=1 \
		-b "${ANYKERNEL_BRANCH_PREFIX}-${TARGET}" \
		"${ANYKERNEL_REPO}" \
		AnyKernel
}

clone_sultan_patches() {
        msg "Cloning sultan patches"

        git clone \
                --depth=1 \
                "${PATCHES2_REPO}" \
                sultan_patches
}

clone_kernel_patches() {
	msg "Cloning kernel patches"

	git clone \
		--depth=1 \
		"${PATCHES_REPO}" \
		kernel_patches
}

clone_nomount() {
	msg "Cloning Nomount"

	setup_script="$(mktemp)"
    trap 'rm -f "$setup_script"' RETURN

    curl \
        --fail \
        --location \
        --silent \
        --show-error \
        --retry 5 \
        --retry-delay 5 \
        --retry-all-errors \
        "https://raw.githubusercontent.com/maxsteeel/nomount/refs/heads/dev/kernel/setup.sh" \
        -o "$setup_script"

    bash "$setup_script" "$NOMOUNT_BRANCH"

	}

clone_susfs() {
	msg "Cloning SUSFS"

	git clone \
		--depth=1 \
		-b "${SUSFS_BRANCH}" \
		"${SUSFS_REPO}"
}

clone_vpnhide() {
        msg "Cloning VPNHide"

        git clone \
                --depth=1 \
                -b "${VPNHIDE_BRANCH}" \
                "${VPNHIDE_REPO}"
}

install_ksu() {
    msg "Cloning KernelSU"

    local setup_script
    setup_script="$(mktemp)"
    trap 'rm -f "$setup_script"' RETURN

    curl \
        --fail \
        --location \
        --silent \
        --show-error \
        --retry 5 \
        --retry-delay 5 \
        --retry-all-errors \
        "https://raw.githubusercontent.com/tiann/KernelSU/main/kernel/setup.sh" \
        -o "$setup_script"

    bash "$setup_script" "$KSU_BRANCH"
}

install_ksu_next() {
    msg "Cloning KernelSU-Next"

    local setup_script
    setup_script="$(mktemp)"
    trap 'rm -f "$setup_script"' RETURN

    curl \
        --fail \
        --location \
        --silent \
        --show-error \
        --retry 5 \
        --retry-delay 5 \
        --retry-all-errors \
		"https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next/next/kernel/setup.sh" \
        -o "$setup_script"

    bash "$setup_script" "$KSU_NEXT_BRANCH"
}

patch_utf8() {
	msg "Patching UTF8"

	patch -p1 < "$KERNEL_REPO"/kernel_patches/common/unicode_bypass_fix_6.1+.patch || true
}

apply_patch() {
	local dir="$1"
	local patchfile="$2"

	patch -d "$dir" -p1 < "$patchfile"
}

apply_patch_optional() {
	local dir="$1"
	local patchfile="$2"

	if ! apply_patch "$dir" "$patchfile"; then
		warn "Patch $(basename "$patchfile") applied with rejects (continuing)."
	fi
}

##COPY SUSFS only used in functions
copy_susfs() {
        cp "$KERNEL_REPO"/susfs4ksu/kernel_patches/fs/* "$KERNEL_REPO"/fs/
        cp "$KERNEL_REPO"/susfs4ksu/kernel_patches/include/linux/* "$KERNEL_REPO"/include/linux/
}
##

patch_susfs_ksu() {
	msg "Copying susfs libs"
	copy_susfs
	msg "Applying SUSFS kernel patch"
	apply_patch_optional \
		"$KERNEL_REPO" \
		"$KERNEL_REPO/susfs4ksu/kernel_patches/50_add_susfs_in_gki-android14-6.1.patch"

	msg "Applying SUSFS KernelSU patch"
	apply_patch_optional \
		"$KERNEL_REPO/KernelSU" \
		"$KERNEL_REPO/susfs4ksu/kernel_patches/KernelSU/10_enable_susfs_for_ksu.patch"
}

patch_susfs_ksu_next() {
	msg "Copying sufs libs"
	copy_susfs

	msg "Applying SUSFS KernelSU-Next integration"

    apply_patch_optional \
        "$KERNEL_REPO" \
        "$KERNEL_REPO/susfs4ksu/kernel_patches/50_add_susfs_in_gki-android14-6.1.patch"

	apply_patch_optional \
		"$KERNEL_REPO/KernelSU-Next" \
		"$KERNEL_REPO/susfs4ksu/kernel_patches/KernelSU/10_enable_susfs_for_ksu.patch"

	msg "Applying KernelSU-Next compatibility fixes"

	apply_patch_optional \
		"$KERNEL_REPO/KernelSU-Next" \
		"$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/fix_Kbuild.patch"

	apply_patch_optional \
		"$KERNEL_REPO/KernelSU-Next" \
		"$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/fix_dispatch.c.patch"

	apply_patch_optional \
                "$KERNEL_REPO/KernelSU-Next" \
                "$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/fix_init.c.patch"

        apply_patch_optional \
                "$KERNEL_REPO/KernelSU-Next" \
        	"$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/fix_kernel_umount.c.patch"

        apply_patch_optional \
                "$KERNEL_REPO/KernelSU-Next" \
                "$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/fix_setuid_hook.c.patch"

        apply_patch_optional \
                "$KERNEL_REPO/KernelSU-Next" \
                "$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/fix_sucompat.c.patch"

        apply_patch_optional \
                "$KERNEL_REPO/KernelSU-Next" \
                "$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/fix_supercall.c.patch"

        apply_patch_optional \
                "$KERNEL_REPO/KernelSU-Next" \
                "$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/fix_selinux.c.patch"

        apply_patch_optional \
                "$KERNEL_REPO/KernelSU-Next" \
                "$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/fix_ksu.h.patch"

        apply_patch_optional \
                "$KERNEL_REPO/KernelSU-Next" \
                "$KERNEL_REPO/kernel_patches/next/susfs_fix_patches/v2.2.0/ksu_toolkit.patch"

}

patch_sultan() {
	msg "Applying Sultan specific patches (fs/namespace.c, kernel/sys.c, fs/super.c)"
	apply_patch_optional \
		"$KERNEL_REPO" \
		"$KERNEL_REPO/sultan_patches/fixer.patch"

	apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO/sultan_patches/fix_super.c.patch"
}

patch_vpnhide() {
	msg "Applying VPNHide"
	cp "$KERNEL_REPO"/vpnhide/builtin/include/linux/vpnhide.h "$KERNEL_REPO"/include/linux/
	cp -r "$KERNEL_REPO"/vpnhide/builtin/security/vpnhide "$KERNEL_REPO"/security/
	apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/fs_namei.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/fs_readdir.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/fs_stat.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_core_dev_ioctl.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_core_fib_rules.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_core_rtnetlink.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_ipv4_devinet.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_ipv4_fib_semantics.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_ipv4_fib_trie.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_ipv6_addrconf.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_ipv6_ip6_fib.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_ipv6_route.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/vpnhide/builtin/versions/android14-6.1/net_socket.c.patch
        apply_patch_optional \
                "$KERNEL_REPO" \
                "$KERNEL_REPO"/kernel_patches/sultan/vpnhide_fs_namei.c.patch
}

######################################################

#clone_kernel_patches
clone_kernel_patches
clone_sultan_patches

case "$VARIANT" in
    ksu-susfs)
	install_ksu
	clone_susfs
	patch_utf8
	patch_susfs_ksu
	patch_sultan
	clone_nomount
	msg "$TARGET $VARIANT done"
        ;;
	ksu-susfs-vpnhide)
	install_ksu
	clone_susfs
	patch_utf8
	patch_susfs_ksu
	patch_sultan
	clone_nomount
	clone_vpnhide
	patch_vpnhide
	msg "$TARGET $VARIANT done"
		;;
    ksu-next-susfs)
	install_ksu_next
	clone_susfs
	patch_utf8
	patch_susfs_ksu_next
	patch_sultan
	clone_nomount
	echo "$TARGET $VARIANT done"
        ;;
	ksu-next-susfs-vpnhide)
	install_ksu_next
	clone_susfs
	patch_utf8
	patch_susfs_ksu_next
	patch_sultan
	clone_nomount
	clone_vpnhide
	patch_vpnhide
	echo "$TARGET $VARIANT done"
        ;;
esac

##Patch Defconfig##

DEFCONFIG="$KERNEL_REPO/arch/arm64/configs/${TARGET}_defconfig"

#KSU
if ! grep -q "^CONFIG_KSU=y$" "$DEFCONFIG"; then
        echo "CONFIG_KSU=y" >> "$DEFCONFIG"
fi

#SUSFS
if ! grep -q "^CONFIG_KSU_SUSFS_SUS_SU=n$" "$DEFCONFIG"; then
        echo "CONFIG_KSU_SUSFS_SUS_SU=n" >> "$DEFCONFIG"
fi

if ! grep -q "^CONFIG_KSU_SUSFS=y$" "$DEFCONFIG"; then
        echo "CONFIG_KSU_SUSFS=y" >> "$DEFCONFIG"
fi

#nomount
if ! grep -q "^CONFIG_NOMOUNT=y$" "$DEFCONFIG"; then
        echo "CONFIG_NOMOUNT=y" >> "$DEFCONFIG"
fi

if [[ "$VARIANT" == *"-vpnhide" ]]; then
                if ! grep -q "^CONFIG_VPNHIDE=y$" "$DEFCONFIG"; then
                        echo "CONFIG_VPNHIDE=y" >> "$DEFCONFIG"
                fi
fi

##fetch anykernel
cd "$KERNEL_REPO"
clone_anykernel
