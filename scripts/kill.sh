#!/usr/bin/env bash
# Kill any stray proxy and VICE processes.
pkill -f "claude_proxy.py" 2>/dev/null || true
pkill -f "x64sc"           2>/dev/null || true
pkill -f "x64"             2>/dev/null || true
echo "done"
