#!/bin/zsh
# Play this worktree's DOA3 build with converted DOAXBV girls.
# In game: F5 cycles player 1, F6 cycles player 2 (stock + every private/doax-*.dxc).
#   play-costume.sh                    both fighters start as Hitomi
#   play-costume.sh christie lisa      P1 / P2
#   play-costume.sh tina -             P1 only (P2 starts stock)
#   play-costume.sh off                both start stock
# Girls: hitomi hitomi-pink christie lisa lisa2 kasumi kasumi2 tina ayane helena leifang
cd "${0:A:h}/.."
export RECOMP_COSTUME_DIR="$PWD/private"
asset() { echo "$PWD/private/doax-${1:-hitomi}.dxc"; }
if [[ "$1" != off ]]; then
  if [[ $# -eq 0 ]]; then export RECOMP_COSTUME_INJECT="$(asset)"
  else
    [[ "$1" != - ]] && export RECOMP_COSTUME_INJECT_P1="$(asset $1)"
    [[ -n "$2" && "$2" != - ]] && export RECOMP_COSTUME_INJECT_P2="$(asset $2)"
    [[ -z "$2" ]] && export RECOMP_COSTUME_INJECT_P2="$(asset $1)"
  fi
fi
exec ./run_game.sh --vsync
