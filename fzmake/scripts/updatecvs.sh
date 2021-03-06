#! /bin/sh

updatecvs()
{
  logprint "Updating source repositories"
  logprint "----------------------------"

  resetPackages || return 1
  while getPackage; do
    logprint "Updating package $PACKAGE"

    if ! [ -d "$CVSDIR/${PACKAGE_REPO}" ]; then
      logprint "Error: $CVSDIR/${PACKAGE_REPO} does not exist"
      return 1
    fi

    cd $CVSDIR/${PACKAGE_REPO}
    if [ -d "CVS" ]; then
      cvs -q -z3 update -dP >> $LOG 2>&1 || return 1
    elif [ -d ".svn" ]; then
      svn update >> $LOG 2>&1 || return 1
    elif [ -d ".git" ]; then
      git pull  >> $LOG 2>&1 || return 1
    elif ! [ -f ".norepo" ]; then
      logprint "Unknown repository type for package $PACKAGE"
      return 1
    fi
  done
}
