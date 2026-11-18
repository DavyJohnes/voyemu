#!/bin/sh
LD_PRELOAD=/log/voyemu-sysmgr/wait.so${LD_PRELOAD:+:$LD_PRELOAD}
export LD_PRELOAD
exec /log/voyemu-sysmgr/sysmgrtool.oem "$@"
