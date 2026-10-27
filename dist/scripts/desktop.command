#!/bin/sh
# P96Multicast: the shared desktop's two halves, a window each.
# For FS-UAE's NAT network (the card sends to 10.0.2.2). On a real network,
# with multicast, use desktop-lan.command instead.
# On a Mac: double-click it (or: sh desktop.command).
cd "$(dirname "$0")" || exit 1
exec python3 p96view.py --span 2x1 --group 127.0.0.1 "$@"
