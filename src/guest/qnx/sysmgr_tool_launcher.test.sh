#!/bin/sh
set -eu
source=$1
work=$(mktemp -d "${TMPDIR:-/tmp}/voyemu-sysmgr-script.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/oem" <<'OEM'
#!/bin/sh
printf '%s\000' "$LD_PRELOAD" "$LD_LIBRARY_PATH" "$SENTINEL" "$#" "$@"
exit 37
OEM
chmod 700 "$work/oem"
sed "s|/log/voyemu-sysmgr/sysmgrtool.oem|$work/oem|" "$source" > "$work/launcher"
chmod 700 "$work/launcher"
before=${LD_PRELOAD-unset}
for mode in unset empty inherited; do
    if (
        unset LD_PRELOAD
        case "$mode" in
            empty) LD_PRELOAD=; export LD_PRELOAD ;;
            inherited) LD_PRELOAD='ancestor:other'; export LD_PRELOAD ;;
        esac
        LD_LIBRARY_PATH='library path'; SENTINEL='keep * value'
        export LD_LIBRARY_PATH SENTINEL
        "$work/launcher" stop '' 'a b' '*?[x]' 'x$`";' 'кириллица' 'line
break'
    ) > "$work/actual" 2>/dev/null; then code=0; else code=$?; fi
    [ "$code" = 37 ]
    preload=/log/voyemu-sysmgr/wait.so
    [ "$mode" != inherited ] || preload=$preload:ancestor:other
    printf '%s\000' "$preload" 'library path' 'keep * value' 7 stop '' 'a b' '*?[x]' 'x$`";' 'кириллица' 'line
break' > "$work/expected"
    cmp "$work/expected" "$work/actual"
done
[ "${LD_PRELOAD-unset}" = "$before" ]
echo 'fixed shell launcher preserves arguments, inherited environment and native exit status'
