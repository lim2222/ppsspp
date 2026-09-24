#!/bin/bash

# Run this inside git bash.

BUILD_ANDROID_PLATFORM="21"
HOST_TAG="windows-x86_64"
TRIPLE="x86_64-linux-android"
TARGET="$TRIPLE$BUILD_ANDROID_PLATFORM"

if [ "$NDK" = "" ]; then
    NDK=/c/Android/sdk/ndk/29.0.14206865
fi
if [ "$NDK_USR_LIB" = "" ]; then
    NDK_USR_LIB=$NDK/toolchains/llvm/prebuilt/$HOST_TAG/sysroot/usr/lib/x86_64-linux-android/$BUILD_ANDROID_PLATFORM
fi
if [ "$NDK_PREBUILT" = "" ]; then
    NDK_PREBUILT=$NDK/toolchains/llvm/prebuilt/$HOST_TAG
fi

set -e

GENERAL="\
   --enable-cross-compile \
   --enable-pic \
   --disable-asm \
   --extra-libs="-latomic" \
   --cc=$NDK_PREBUILT/bin/clang \
   --ld=$NDK_PREBUILT/bin/clang \
   --nm=$NDK_PREBUILT/bin/llvm-nm \
   --ar=$NDK_PREBUILT/bin/llvm-ar \
   --ranlib=$NDK_PREBUILT/bin/llvm-ranlib"

MODULES="\
   --disable-avdevice \
   --disable-filters \
   --disable-programs \
   --disable-network \
   --disable-avfilter \
   --disable-postproc \
   --disable-encoders \
   --disable-protocols \
   --disable-hwaccels \
   --disable-doc"

VIDEO_DECODERS="\
   --enable-decoder=h264 \
   --enable-decoder=mpeg4 \
   --enable-decoder=mpeg2video \
   --enable-decoder=mjpeg \
   --enable-decoder=mjpegb"

AUDIO_DECODERS="\
    --enable-decoder=aac \
    --enable-decoder=aac_latm \
    --enable-decoder=atrac3 \
    --enable-decoder=atrac3p \
    --enable-decoder=mp3 \
    --enable-decoder=pcm_s16le \
    --enable-decoder=pcm_s8"

DEMUXERS="\
    --enable-demuxer=h264 \
    --enable-demuxer=m4v \
    --enable-demuxer=mov \
    --enable-demuxer=matroska \
    --enable-demuxer=mpegvideo \
    --enable-demuxer=mpegps \
    --enable-demuxer=mp3 \
    --enable-demuxer=avi \
    --enable-demuxer=aac \
    --enable-demuxer=pmp \
    --enable-demuxer=wav \
    --enable-demuxer=pcm_s16le \
    --enable-demuxer=pcm_s8"

VIDEO_ENCODERS="\
    --enable-encoder=huffyuv \
    --enable-encoder=ffv1"

AUDIO_ENCODERS="\
          --enable-encoder=pcm_s16le"

MUXERS="\
        --enable-muxer=avi"

PARSERS="\
    --enable-parser=h264 \
    --enable-parser=mpeg4video \
    --enable-parser=mpegaudio \
    --enable-parser=mpegvideo \
    --enable-parser=aac \
    --enable-parser=aac_latm"

PROTOCOLS="\
    --enable-protocol=file"

function build_x86_64
{
./configure --logfile=conflog.txt --target-os=linux \
    --prefix=./android/x86_64 \
    --arch=x86_64 \
    ${GENERAL} \
    --extra-cflags=" --target=$TARGET -no-canonical-prefixes -fdata-sections -ffunction-sections -fno-limit-debug-info -funwind-tables -fPIC -O3 -DCONFIG_PIC -DANDROID -DANDROID_PLATFORM=android-$BUILD_ANDROID_PLATFORM -Dipv6mr_interface=ipv6mr_ifindex -fasm -fno-short-enums -fno-strict-aliasing -fomit-frame-pointer -march=x86-64" \
    --disable-shared \
    --enable-static \
    --extra-ldflags="--target=$TARGET -Wl,-Bsymbolic -Wl,-z,max-page-size=16384 -Wl,--rpath-link,$NDK_USR_LIB -L$NDK_USR_LIB -nostdlib -lc -lm -ldl -llog" \
    --enable-zlib \
    --disable-everything \
    ${MODULES} \
    ${VIDEO_DECODERS} \
    ${AUDIO_DECODERS} \
    ${VIDEO_ENCODERS} \
    ${AUDIO_ENCODERS} \
    ${DEMUXERS} \
    ${MUXERS} \
    ${PARSERS} \
    ${PROTOCOLS}

make clean
make -j16 install
}

build_x86_64

echo Android x86_64 build finished