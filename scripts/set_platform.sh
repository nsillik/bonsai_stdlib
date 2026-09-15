#! /bin/bash

UNAME=$(uname)
Platform=$UNAME
if [ "$UNAME" == "Linux" ] ; then
  Platform="Linux"
elif [[ "$UNAME" == CYGWIN* || "$UNAME" == MINGW* || "$UNAME" == MSYS* ]] ; then
  Platform="Windows"
elif [ "$UNAME" == "Darwin" ] ; then
  # NOTE(nsillik)(macos): "macOS" and not "Darwin", so release bundles read
  # macOS_x86_64_release.tar.gz, symmetric with Linux_x86_64_release.tar.gz.
  Platform="macOS"
fi
