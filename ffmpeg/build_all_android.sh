#!/bin/bash
# 在 MSYS2 / Git Bash 里跑

export NDK=/c/Users/Acer/AppData/Local/Android/Sdk/ndk/29.0.14206865
cd /c/msys64/home/Acer/ppsspp/ffmpeg

echo "=== Building armeabi-v7a ==="
bash android_armeabi-v7a.sh 2>&1 | tee armv7_build_log.txt

echo "=== Building arm64-v8a ==="
bash android_arm64-v8a.sh 2>&1 | tee arm64_build_log.txt

echo "=== Building x86 ==="
bash android_x86.sh 2>&1 | tee x86_build_log.txt

echo "=== Building x86_64 ==="
bash android_x86_64.sh 2>&1 | tee x86_64_build_log.txt

echo "All done! Check *_build_log.txt for errors."