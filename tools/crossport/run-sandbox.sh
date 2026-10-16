#!/bin/zsh
# usage: run-sandbox.sh <seconds> <log> -- extra runner args ; env passes through.
# Restores the sandbox save from the Mac install before each run (never the reverse).
SECS=$1; LOG=$2; shift 3
W=$HOME/code/recomp/doaxvb-re/private/claude-work
[[ -n "$NORESTORE" ]] || rsync -a --delete $HOME/Games/DOAXBV-0.6.0-rc1-macOS/private/imported-disc/disc/.recomp-storage/ $W/sandbox-disc/.recomp-storage/
cd $W/sandbox-disc
RECOMP_HEADLESS=1 RECOMP_AUDIO_GAIN=0 ${RUNNER:-$W/build-unlock/recomp_program_runner} --xbe default.xbe --vsync "$@" > $LOG 2>&1 &
PID=$!; sleep $SECS; kill -INT $PID; sleep 2; kill -9 $PID 2>/dev/null
