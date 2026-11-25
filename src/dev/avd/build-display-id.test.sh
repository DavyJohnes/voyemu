#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C
tool=$1
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cd "$work"

mkdir -p input/system
cat > input/system/build.prop <<'PROPS'
# test build
ro.build.id=SDK-base
ro.build.display.id=sdk_phone_x86_64-userdebug 11 test-keys
ro.build.display.id.extra=keep
ro.build.fingerprint=fixture/sdk/base:11/SDK-base:userdebug/test-keys
PROPS
cat > expected <<'PROPS'
# test build
ro.build.id=SDK-base
ro.build.display.id=fixture-1.2.3
ro.build.display.id.extra=keep
ro.build.fingerprint=fixture/sdk/base:11/SDK-base:userdebug/test-keys
PROPS

make_image() {
    mkfs.ext4 -q -d input "$1" 8M
    cat > ops <<'OPS'
set_inode_field /system/build.prop uid 123
set_inode_field /system/build.prop gid 456
set_inode_field /system/build.prop mode 0100640
ea_set -f expected-label /system/build.prop security.selinux
OPS
    debugfs -w -f ops "$1" >/dev/null 2>error
    [[ $(wc -l < error) == 1 ]]
}

printf '%s\0' u:object_r:system_file:s0 > expected-label
make_image valid.ext4
if ! bash "$tool" valid.ext4 fixture-1.2.3; then
    echo 'Could not set the display ID in a valid ext4 image' >&2
    exit 1
fi
debugfs -R 'cat /system/build.prop' valid.ext4 > actual
cmp expected actual
metadata=$(debugfs -R 'stat /system/build.prop' valid.ext4)
[[ $metadata =~ Mode:[[:space:]]+0640[[:space:]] && $metadata =~ User:[[:space:]]+123[[:space:]]+Group:[[:space:]]+456[[:space:]] ]]
debugfs -R 'ea_get -f actual-label /system/build.prop security.selinux' valid.ext4
cmp expected-label actual-label
e2fsck -fn valid.ext4 >/dev/null

printf '%s\n' 'ro.build.display.id=duplicate' >> input/system/build.prop
make_image duplicate.ext4
sed -i '/^ro\.build\.display\.id=/d' input/system/build.prop
make_image missing.ext4
for image in duplicate.ext4 missing.ext4; do
    sha256sum "$image" > before.sha256
    if bash "$tool" "$image" fixture-1.2.3 > rejected.log 2>&1; then
        echo "Expected rejection of $image" >&2
        exit 1
    fi
    grep -Fq 'Expected one ro.build.display.id' rejected.log
    sha256sum -c before.sha256
done

sha256sum valid.ext4 > before.sha256
if bash "$tool" valid.ext4 $'line1\nline2' > rejected.log 2>&1; then
    echo 'Expected rejection of a multiline display ID' >&2
    exit 1
fi
grep -Fq 'Invalid build display ID' rejected.log
sha256sum -c before.sha256
