#!/usr/bin/env bash
set -euo pipefail
test "${GITHUB_ACTIONS:-}" = true || { printf '%s\n' 'Media compilation is allowed only in GitHub Actions.' >&2; exit 1; }
task_root="$PWD"
test "$(pacman -Q mingw-w64-ucrt-x86_64-rubberband)" = 'mingw-w64-ucrt-x86_64-rubberband 3.3.0-1'
test "$(pacman -Q mingw-w64-ucrt-x86_64-lame)" = 'mingw-w64-ucrt-x86_64-lame 3.100-3'
test "$(pacman -Q mingw-w64-ucrt-x86_64-chromaprint)" = 'mingw-w64-ucrt-x86_64-chromaprint 1.6.1-1'
pacman -Q > vendor/media-packages.txt
cd vendor/ffmpeg
patch -Nbp1 -i "$task_root/ci/ffmpeg-vibrato.patch"
export GIT_DIR=/dev/null
./configure --target-os=mingw32 --arch=x86_64 --cc=gcc --cxx=g++ \
  --disable-autodetect --disable-everything --disable-programs --enable-ffmpeg \
  --disable-doc --disable-debug --disable-shared --enable-static --disable-avdevice \
  --disable-swscale --disable-network --enable-gpl --enable-version3 \
  --enable-librubberband --enable-libmp3lame --enable-chromaprint \
  --pkg-config-flags=--static --extra-ldflags='-static -Wl,--no-insert-timestamp' \
  --enable-demuxers --enable-parsers --enable-protocol=file,pipe \
  --enable-decoder=aac,aac_fixed,aac_latm,ac3,eac3,alac,flac,mp3,mp3float,opus,vorbis,wmav1,wmav2,wmapro,wmalossless,wmavoice,pcm_alaw,pcm_mulaw,pcm_s8,pcm_u8,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_f32le,pcm_f32be,pcm_f64le,pcm_f64be \
  --enable-encoder=pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_f64le,flac,libmp3lame \
  --enable-muxer=wav,mp3,flac,chromaprint,null \
  --enable-filter=abuffer,abuffersink,aformat,aresample,anull,rubberband,vibrato,volume,atempo,asetrate
make -r -j4 ffmpeg.exe
cp ffmpeg.exe "$task_root/vendor/media/ffmpeg.exe"
cp ffbuild/config.log "$task_root/ci-output/media-config.log"
objdump -p ffmpeg.exe > "$task_root/ci-output/media-imports.txt"
if grep -Eqi 'DLL Name:.*(msys|libstdc|libgcc|libwinpthread|rubberband|chromaprint|fftw|samplerate|mp3lame)' "$task_root/ci-output/media-imports.txt"; then
  printf '%s\n' 'The media executable still requires an unbundled runtime.' >&2; exit 1
fi
