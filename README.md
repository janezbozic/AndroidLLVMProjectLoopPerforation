# The LLVM Compiler Infrastructure with loop perforation pass

This is the implementation of LLVM-Project, which includes loop perforation pass.

## How to build this project:

### Build Compilers:
From [Here](https://android.googlesource.com/toolchain/llvm_android/+/master/README.md)
1. mkdir llvm-toolchain && cd llvm-toolchain
2. repo init -u https://android.googlesource.com/platform/manifest -b llvm-toolchain
3. repo sync -c
4. cd toolchain
5. rm -rf llvm-project
6. git clone git@github.com:janezbozic/llvm-project.git or git clone https://github.com/janezbozic/llvm-project.git
7. python toolchain/llvm_android/build.py

### Build NDK:
From [Here](https://android.googlesource.com/platform/ndk/+/ndk-r15-release/README.md)
1. mkdir ndk_source
2. cd ndk_source
3. repo init -u https://android.googlesource.com/platform/manifest \
    -b master-ndk
4. copy clang-dev from llvm-toolchain/out/install/<your_platform> to ndk/prebuilts/host/<your_platform>
5. from ndk folder: cd ndk --> so your path is ndk_source/ndk
6. edit CLANG_VERSION = 'clang-dev' in ndk_source/ndk/ndk/toolchains.py
7. ./checkbuild.py --system <linux/darwin/win32>
8. Add ndkPath parameter to build.gradle file of your android project, pointing at ndk/out/<host>/<ndk_version>
    
