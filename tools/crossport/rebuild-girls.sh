#!/bin/zsh
# Rebuilds every DOAXBV girl asset for DOA3 (current asset format).
cd $HOME/code/recomp/doaxvb-re/private/claude-work/xport
export MESH_DUMP_INDEX_DIR=$HOME/code/recomp/doaxvb-re/private/claude-work/afs-index
D=$HOME/code/recomp/doaxvb-re/private/setup-8569d80610c6/imported-disc/disc
PY=../meshvenv/bin/python
for d in ../meshdump-run7 ../meshdump-dense ../dense-102 ../dense-98 ../dense-50; do
  missing=(); for p in $d/present-*; do [[ -f $p/export.json ]] || missing+=($p); done
  (( ${#missing} )) && $PY mesh_dump_export.py $missing --afs $D/datahdd.afs --afs $D/datadvd.afs >/dev/null 2>&1
done
R=../meshdump-run7; O=$HOME/code/recomp/doa3-re/private/wt-meshdump/private
b() { $PY build_costume_asset.py "$@" 2>&1 | grep "dxc \|soft"; }
b $R/present-2410 $R/present-2350 $R/present-2440 $R/present-1600 $R/present-1660 $R/present-1720 $O/doax-hitomi.dxc --actor 0 --soft ../meshdump-dense
b $R/present-1900 $R/present-2100 $R/present-3800 $R/present-4300 $O/doax-lisa.dxc --actor 3 --soft ../dense-102
b $R/present-4300 $R/present-4000 $O/doax-christie.dxc --actor 2 --soft ../dense-98
b $R/present-4500 $R/present-4000 $R/present-2850 $O/doax-kasumi.dxc --actor 3 --soft ../dense-50
$PY capture_to_asset.py ../dense-86 --game doaxbv --name ayane --actor 1 2>&1 | grep "dxc \|soft"
# Tina: dense-46 alone never draws her feet/right shin (culled); dense-66 2662-2670 and run7 2850 do.
b ../dense-66/present-2662 ../dense-66/present-2666 ../dense-66/present-2670 $R/present-2850 ../dense-46/present-2990 ../dense-46/present-3000 ../dense-46/present-3010 ../dense-46/present-3020 $O/doax-tina.dxc --actor 2 --soft ../dense-46
$PY capture_to_asset.py ../dense-66 --game doaxbv --name helena --actor 3 2>&1 | grep "dxc \|soft"
echo done
b $R/present-1660 $R/present-1600 $R/present-1720 $O/doax-hitomi-pink.dxc --actor 0 --soft ../meshdump-dense
b $R/present-4000 $O/doax-kasumi2.dxc --actor 1
$PY capture_to_asset.py ../dense-98 --game doaxbv --name lisa2 --actor 0 2>&1 | grep "dxc \|soft"
$PY capture_to_asset.py ../dense-leifang --game doaxbv --name leifang --actor 3 2>&1 | grep "dxc \|soft"
# Names verified from hair/skin textures (girls change actor slots between runs).
