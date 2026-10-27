#!/bin/sh
# P96Multicast: the shared desktop's two halves, a window each, from the
# multicast groups 239.255.96.1 and .2 (a real network, or a bridged FS-UAE).
cd "$(dirname "$0")" || exit 1
exec python3 p96view.py --span 2x1 "$@"
