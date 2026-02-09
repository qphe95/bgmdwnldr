#!/system/bin/sh
HERE="$(cd "$(dirname "$0")" && pwd)"
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=0:print_legend=1:halt_on_error=0
exec "$@"
