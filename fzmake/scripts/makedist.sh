#! /bin/sh

makedist_package()
{
  logprint "Creating source distribution"
  logprint "Configuring $1 package"

  mkdir -p "$WORKDIR/dist/$1"

  cd "$WORKDIR/dist/$1"
  ../../source/$1/configure --enable-localesonly >> $LOG 2>&1 || return 1
  make dist >> $LOG 2>&1 || return 1

  cp `find *.tar.bz2` "$OUTPUTDIR/$2" >> $LOG 2>&1 || return 1

  return 0
}

makedist()
{
  makedist_package lfz libfilezilla-src.tar.bz2 || return 1
  makedist_package FileZilla3 FileZilla3-src.tar.bz2 || return 1
}
