#! /bin/sh

PACKAGES_FILE="$SCRIPTS/packages"
. "$SCRIPTS/readpackages"
. "$SCRIPTS/util.sh"
. "$SCRIPTS/readfile.sh"
. "$SCRIPTS/postbuild.sh"

makepackage()
{
  PACKAGE=$1
  FLAGS=$2

  NOINST=
  if [ "${PACKAGE_TYPE}" = "-" ]; then
    NOINST="yes"
  fi

  echo "Building $PACKAGE for $TARGET"

  mkdir -p "$WORKDIR/$PACKAGE"
  cd "$WORKDIR/$PACKAGE"

  HOSTARG=`"$SCRIPTS/configure_target.sh" "$TARGET"`

  top_srcdir=$(relpath "$(pwd)" "$PREFIX/packages/$PACKAGE/")

  if ! eval "${top_srcdir}/configure" "'--prefix=$WORKDIR/prefix/$PACKAGE'" $HOSTARG $FLAGS; then
    if [ -f config.log ]; then
      cat config.log
    fi
    return 1
  fi
  if [ -z "$MAKE" ]; then
    MAKE=make
  fi

  echo "Build command: nice \"$MAKE\" -j`cpu_count`"
  nice "$MAKE" -j`cpu_count` || return 1
  if grep '^check:' Makefile >/dev/null 2>&1; then
    if ! nice "$MAKE" -j`cpu_count` check; then
      if [ -f tests/test-suite.log ]; then
        cat tests/test-suite.log
      fi
      exit 1
    fi
  fi

  mkdir -p "$WORKDIR/prefix/$PACKAGE/bin"
  mkdir -p "$WORKDIR/prefix/$PACKAGE/lib"
  nice "$MAKE" install || return 1

  if [ -z "$NOINST" ]; then
    echo "Running postbuild script"

    clientpostbuild "$PACKAGE" || return 1
  fi
}

while getPackage; do
  makepackage $PACKAGE "$PACKAGE_CONFIGURE_FLAGS" || exit 1

  PATH="$WORKDIR/prefix/$PACKAGE/bin:$PATH"
  LD_LIBRARY_PATH="$WORKDIR/prefix/$PACKAGE/lib:$LD_LIBRARY_PATH"
  PKG_CONFIG_PATH="$WORKDIR/prefix/$PACKAGE/lib/pkgconfig:$PKG_CONFIG_PATH"
done

