#! /bin/sh

makedist_package()
{
  logprint "Creating source distribution"
  logprint "Configuring $1 package"

  mkdir -p "$WORKDIR/dist/$1"

  cd "$WORKDIR/dist/$1"
  ../../source/$1/configure $2 >> $LOG 2>&1 || return 1
  make dist >> $LOG 2>&1 || return 1

  cp `find *.tar.bz2` "$OUTPUTDIR/$3" >> $LOG 2>&1 || return 1

  return 0
}

makedist()
{
  resetPackages || return 1

  while getPackage; do
    if packageHasFlag "dist"; then
      FLAGARG="${FLAGARG:---enable-localesonly}"
      makedist_package "${PACKAGE}" $FLAGARG "${PACKAGE}_snapshot_${SHORTDATE}_src.tar.bz2" || return 1
    fi
  done

  return 0
}
