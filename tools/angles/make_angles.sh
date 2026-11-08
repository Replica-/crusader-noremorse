#!/usr/bin/env bash
# The EXTRA ANGLES option: the Silencer's combat stance in 64 facings instead of 16. From the 16 drawn facings of each
# gun class (angleexport), RIFE makes the three in between each neighbouring pair (a quarter, a half and three quarters
# of the way), for the picture and for its outline (RIFE has no alpha), merged back into one RGBA picture:
# assets/angles/<set>_sNN.png, NN = 4 x facing + 1..3 (the drawn ones, NN % 4 == 0, are the game's own).
#
#   tools/angles/make_angles.sh <game dir> <rife-ncnn-vulkan dir> [out dir]
#
# Needs ffmpeg on the PATH and build/angleexport.
set -euo pipefail
game=${1:?game dir}
rife=${2:?rife-ncnn-vulkan dir}
out=${3:-assets/angles}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

build/angleexport "$game" "$work/comp" >/dev/null
mkdir -p "$out" "$work/rgb" "$work/mask" "$work/mid"
for f in "$work"/comp/*.png; do
    n=$(basename "$f")
    # The picture over black, and its outline in white.
    ffmpeg -loglevel error -y -f lavfi -i color=black:s=160x160 -i "$f" \
        -filter_complex "[0][1]overlay=format=auto,format=rgb24" -frames:v 1 "$work/rgb/$n"
    ffmpeg -loglevel error -y -i "$f" -vf "format=rgba,alphaextract,format=rgb24" "$work/mask/$n"
done
for set in c0 c1 c2 c3 c4 cn; do
    for d in $(seq 0 15); do
        e=$(( (d + 1) % 16 ))
        a=$(printf "%s_d%02d.png" "$set" "$d")
        b=$(printf "%s_d%02d.png" "$set" "$e")
        for k in 1 2 3; do
            s=$(awk "BEGIN { print $k / 4 }")
            step=$(printf "%02d" $(( d * 4 + k )))
            "$rife/rife-ncnn-vulkan" -0 "$work/rgb/$a" -1 "$work/rgb/$b" -o "$work/mid/rgb.png" -s "$s" -m "$rife/rife-v4.6" >/dev/null 2>&1
            "$rife/rife-ncnn-vulkan" -0 "$work/mask/$a" -1 "$work/mask/$b" -o "$work/mid/mask.png" -s "$s" -m "$rife/rife-v4.6" >/dev/null 2>&1
            ffmpeg -loglevel error -y -i "$work/mid/rgb.png" -i "$work/mid/mask.png" \
                -filter_complex "[1]format=gray[m];[0][m]alphamerge" "$out/${set}_s$step.png"
        done
    done
    echo "$set: 48 in-between facings"
done
