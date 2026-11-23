#!/bin/sh
# Saturn Duck TrueMotion AVIs (half-width 144x180 coded) -> SDLPAL-playable MS Video-1 16-bit AVIs
# A1..A6 -> 1..6.avi (same roles as Win95 PAL: logo, opening, new-game intro, ending x2, credits)
DISC=$1; OUT=$2
for i in 1 2 3 4 5 6; do
  # The disc files contain tiny packets that are not pictures: 15-byte "no change" frames and 23/25-byte
  # compression-0 (no-op) frames. ffmpeg's TrueMotion1 decoder decodes the first kind from bytes past the packet
  # and resets its picture on the second, which left rainbow-coloured blocks until the next keyframe (2026-11-23).
  # Drop them (every real frame is > 400 bytes); -fps_mode cfr repeats the previous picture in their place.
  ffmpeg -v error -y -bsf:v "noise=drop='lt(size,32)'" -i "$DISC/A$i.AVI" -fps_mode cfr \
    -vf "scale=288:180:flags=bicubic,pad=320:200:16:10:black" \
    -c:v msvideo1 -pix_fmt rgb555le -q:v 2 \
    -map_metadata -1 -fflags +bitexact -c:a pcm_s16le -ar 44100 -ac 2 "$OUT/$i.avi" && ls -la "$OUT/$i.avi"
done
