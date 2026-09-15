#! /bin/bash

UNAME=$(uname)
Platform=$UNAME
if [ "$UNAME" == "Linux" ] ; then
  Platform="Linux"
elif [[ "$UNAME" == CYGWIN* || "$UNAME" == MINGW* || "$UNAME" == MSYS* ]] ; then
  Platform="Windows"
elif [ "$UNAME" == "Darwin" ] ; then
  # NOTE(nsillik): Deliberately not "Darwin", so bundle names read
  # macOS_x86_64_release.tar.gz, symmetric with Linux_x86_64_release.tar.gz
  Platform="macOS"
fi

# Architecture of the binaries this build produces, normalized to arm64 | x86_64.
# (uname -m reports arm64 on macOS, aarch64 on Linux, and can report AMD64 under
# MSYS.)  Used for `-target` and for release bundle naming.
ARCH=$(uname -m)
case "$ARCH" in
  arm64|aarch64)       ARCH="arm64"  ;;
  x86_64|amd64|AMD64)  ARCH="x86_64" ;;
esac

# NOTE(nsillik)(macos): Phase 1 always builds x86_64 on macOS, including on Apple
# Silicon, where the result runs under Rosetta 2.  The SIMD layer is SSE/AVX-only
# and `-mssse3 -mavx -mavx2 -mfma` are hard errors for an arm64 target, so this
# is the target arch, not the host arch.  Phase 4 adds NEON and deletes this.
if [ "$Platform" == "macOS" ] && [ "$ARCH" == "arm64" ] ; then
  ARCH="x86_64"
fi
