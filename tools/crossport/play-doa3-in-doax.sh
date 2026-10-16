#!/bin/zsh
# DOAXBV (mesh-dump build) with DOA3 characters on the girls. Uses the sandbox
# disc folder (its own saves), never your real install or saves.
#   play-doa3-in-doax.sh            start as Helena; F5 cycles every DOA3 character + stock
#   play-doa3-in-doax.sh leifang    start as doa3-costumes/doa3-leifang.d3c
#   play-doa3-in-doax.sh off        start stock (F5 still cycles; use this to F9-capture a girl)
W=$HOME/code/recomp/doaxvb-re/private/claude-work
export RECOMP_COSTUME_DIR=$W/doa3-costumes
case "$1" in
  off) ;;
  "") export RECOMP_COSTUME_INJECT=$W/doa3-costumes/doa3-helena.d3c ;;
  *) export RECOMP_COSTUME_INJECT=$W/doa3-costumes/doa3-$1.d3c ;;
esac
cd $W/sandbox-disc
exec $W/build-mac-meshdump/recomp_program_runner --xbe default.xbe --vsync
