#!/bin/zsh
# Dense-dumps each DOA3 attract scene and converts both fighters for DOAXBV.
G=$HOME/code/recomp/doa3-re/private/wt-meshdump
X=$HOME/code/recomp/doaxvb-re/private/claude-work/xport
for s in 12330 13500 15350 16900 19900; do
  O=$G/private/harvest/scene-$s
  rm -rf $O; mkdir -p $O
  cd $G
  RECOMP_HEADLESS=1 RECOMP_AUDIO_GAIN=0 RECOMP_MESH_DUMP=$O RECOMP_MESH_DUMP_RAM=1 \
    RECOMP_MESH_DUMP_AT=$(seq -s, $s 2 $((s+30))) \
    ./build/recomp-program/recomp_program_runner --xbe private/play-disc/default.xbe --vsync > $O/log.txt 2>&1 &
  PID=$!; sleep $(( s/60 + 30 )); kill -INT $PID; sleep 2; kill -9 $PID 2>/dev/null
  cd $X && ../meshvenv/bin/python capture_to_asset.py $O --game doa3 --all --name scene-$s 2>&1 | grep "d3c \|per fighter"
done
echo done
