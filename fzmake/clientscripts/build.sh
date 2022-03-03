#! /bin/bash

shopt -s nullglob

export PREFIX="$1"
export TARGET="$2"
export FTARGET="${TARGET##*/}"

CHROOTED="$3"

export SCRIPTS="$PREFIX/clientscripts"
export WORKDIR="$PREFIX/work"
export OUTPUTDIR="$PREFIX/output"

echo "Clientscript forked"
echo "Making sure environment is sane"

if which caffeinate > /dev/null 2>&1; then
  # Prevent OS X from falling asleep
  caffeinate -i -w $$ &
fi

. "$SCRIPTS/parameters"

safe_prepend()
{
  local VAR=$1
  local VALUE=$2
  local SEPARATOR=$3
  local OLD

  # Make sure it's terminated by a separator
  eval OLD=\"\$$VAR$SEPARATOR\"
  while [ ! -z "$OLD" ]; do

    # Get first segment
    FIRST="${OLD%%$SEPARATOR*}"
    if [ "$FIRST" = "$VALUE" ]; then
      return
    fi

    # Strip first
    OLD="${OLD#*$SEPARATOR}"
  done

  eval export $VAR=\"$VALUE\${$VAR:+$SEPARATOR}\$$VAR\"
}

if ! [ -d "$PREFIX" ]; then
  echo "\$PREFIX does not exist, check config/hosts"
  exit 1
fi

# Adding $TARGET's prefix
unset TARGET_PREFIX
if [ ! -z "$HOME" ]; then
  if [ -d "$HOME/$TARGET" ]; then
    export TARGET_PREFIX="$HOME/$TARGET"
  elif [ -d "$HOME/prefix-$TARGET" ]; then
    export TARGET_PREFIX="$HOME/prefix-$TARGET"
  elif [ -d "$HOME/prefix/$TARGET" ]; then
    export TARGET_PREFIX="$HOME/prefix/$TARGET"
  fi

  if [ ! -z "TARGET_PREFIX" ]; then
    echo "Found target specific prefix for $TARGET"
    if [ -f "$TARGET_PREFIX/SCHROOT" ]; then
      if [ -z "$CHROOTED" ]; then
        echo "Changing root directory"
        schroot -c "`cat \"$TARGET_PREFIX/SCHROOT\"`" $0 "$PREFIX" "$TARGET" 1
        echo "Returned from chroot"
        exit $?
      fi
    fi

    if [ -x "$TARGET_PREFIX/profile" ]; then
      . "$TARGET_PREFIX/profile"
    fi

    safe_prepend PATH "$TARGET_PREFIX/bin" ':'
    safe_prepend CPPFLAGS "-I$TARGET_PREFIX/include" ' '
    safe_prepend LDFLAGS "-L$TARGET_PREFIX/lib" ' '
    safe_prepend LD_LIBRARY_PATH "$TARGET_PREFIX/lib" ':'
    safe_prepend PKG_CONFIG_PATH "$TARGET_PREFIX/lib/pkgconfig" ':'
  fi
fi

echo "CPPFLAGS: $CPPFLAGS"
echo "CFLAGS: $CFLAGS"
echo "CXXFLAGS: $CXXFLAGS"
echo "LDFLAGS: $LDFLAGS"
echo "LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo "PKG_CONFIG_PATH: $PKG_CONFIG_PATH"

rm -rf "$OUTPUTDIR"
mkdir "$OUTPUTDIR"

if [ -e "$HOME/.bash_profile" ]; then
  . "$HOME/.bash_profile"
fi

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"

echo "Making target $TARGET"
$SCRIPTS/target.sh || exit 1

echo "Making tarball of output files"
cd "$OUTPUTDIR"
tar -cf "$PREFIX/output.tar" * || exit 1

SIZE=`ls -nl "$PREFIX/output.tar" | awk '{print $5}'`
echo "Size of output: $SIZE bytes"

echo "Cleanup"
rm -rf "$WORKDIR"


