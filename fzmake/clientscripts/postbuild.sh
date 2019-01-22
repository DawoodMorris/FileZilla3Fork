#! /bin/sh

set -e

TARGET=$1
PACKAGE=$2

mkdir -p "$OUTPUTDIR/$TARGET"

if [ -x "`which sha512sum`" ]; then
  SHASUM="sha512sum --binary"
else
  SHASUM="shasum -a 512 -b"
fi

if [ "$STRIP" = "false" ]; then
  echo "Binaries will not be stripped"
elif [ "$STRIP" = "" ]; then
  export STRIP=`which strip`
  echo "Binaries will be stripped using default \"$STRIP\""
  if [ ! -x "$STRIP" ]; then
    echo "Stripping program does not exist or is not marked executable."
    exit 1
  fi
else
  export STRIP=`which "$STRIP"`
  echo "Binaries will be stripped using \"$STRIP\""
  if [ ! -x "$STRIP" ]; then
    echo "Stripping program does not exist or is not marked executable."
    exit 1
  fi
fi

do_strip()
{
  if echo "$TARGET" | grep apple-darwin 2>&1 > /dev/null; then
    if ! echo "$1/$2" | grep "\\.dylib" 2>&1 > /dev/null; then
      echo "Creating debug symbols for \"$1/$2\""
      dsymutil "$1/$2"
    fi
  fi

  if [ -d "$3" ]; then
    if [ -f "$1/.libs/$2" ]; then
      cp "$1/.libs/$2" "$3/$2"
    elif [ -f "$1/$2" ]; then
      cp "$1/$2" "$3/$2"
    fi
    if [ -d "$1/$2.dSYM" ]; then
      cp -r "$1/$2.dSYM" "$3/$2.dSYM"
    fi
  fi

  if [ "$STRIP" = "false" ]; then
    return
  fi

  if [ -f "$1/$2" ]; then
    if ! "$STRIP" "$1/$2"; then
      echo "Stripping of \"$1/$2\" failed."
      exit 1
    fi
  fi
  if [ -f "$1/.libs/$2" ]; then
    if ! "$STRIP" "$1/.libs/$2"; then
      echo "Stripping of \"$1/.libs/$2\" failed."
      exit 1
    fi
  fi

  if [ -d "$1/$2.dSYM" ]; then
    rm -rf "$1/$2.dSYM"
  fi
}

do_sign()
{
  if ! [ -x "$HOME/prefix-$TARGET/sign.sh" ]; then
    return
  fi

  if [ -f "$1/.libs/$2" ]; then
    echo "Signing $1/$2"
    "$HOME/prefix-$TARGET/sign.sh" "$1/.libs/$2" || exit 1
  elif [ -f "$1/$2" ]; then
    echo "Signing $1/$2"
    "$HOME/prefix-$TARGET/sign.sh" "$1/$2" || exit 1
  fi
}

copy_so()
{
  name=${1##*/}
  if [ ! -f "$PACKAGE/lib/$name" ]; then
    echo "Copying dependency $name"
    cp "$1" "$PACKAGE/lib/$name"

    do_strip "$PACKAGE/lib/" "$name" "$WORKDIR/debug"
  fi
}

copy_sos()
{
 while [ ! -z "$1" ]; do
    copy_so "$1"
    shift
  done
}

rm -rf "$WORKDIR/debug"
mkdir "$WORKDIR/debug"

if echo "$TARGET" | grep "mingw"; then

  MAKENSIS="wine /home/nightlybuild/nsis-3.01/makensis.exe"

  do_strip "$WORKDIR/$PACKAGE/src/interface" "filezilla.exe" "$WORKDIR/debug"
  do_strip "$WORKDIR/$PACKAGE/src/putty" "fzputtygen.exe" "$WORKDIR/debug"
  do_strip "$WORKDIR/$PACKAGE/src/putty" "fzsftp.exe" "$WORKDIR/debug"
  do_strip "$WORKDIR/$PACKAGE/src/storj" "fzstorj.exe" "$WORKDIR/debug"
  do_strip "$WORKDIR/$PACKAGE/src/fzshellext/32" "libfzshellext-0.dll" "$WORKDIR/debug"
  do_strip "$WORKDIR/$PACKAGE/src/fzshellext/64" "libfzshellext-0.dll" "$WORKDIR/debug"

  do_sign "$WORKDIR/$PACKAGE/src/interface" "filezilla.exe"
  do_sign "$WORKDIR/$PACKAGE/src/putty" "fzputtygen.exe"
  do_sign "$WORKDIR/$PACKAGE/src/putty" "fzsftp.exe"
  do_sign "$WORKDIR/$PACKAGE/src/storj" "fzstorj.exe"
  do_sign "$WORKDIR/$PACKAGE/src/fzshellext/32" "libfzshellext-0.dll"
  do_sign "$WORKDIR/$PACKAGE/src/fzshellext/64" "libfzshellext-0.dll"

  cd "$WORKDIR/$PACKAGE/data/dlls"
  for i in *.dll; do
    do_strip "$WORKDIR/$PACKAGE/data/dlls" "$i" "$WORKDIR/debug"
    do_sign "$WORKDIR/$PACKAGE/data/dlls" "$i"
  done

  echo "Making installer"
  cd "$WORKDIR/$PACKAGE/data"

  $MAKENSIS install.nsi
  do_sign "$WORKDIR/$PACKAGE/data" "FileZilla_3_setup.exe"
  chmod 775 FileZilla_3_setup.exe
  mv FileZilla_3_setup.exe "$OUTPUTDIR/$TARGET"

  sh makezip.sh "$WORKDIR/prefix/$PACKAGE" || exit 1
  mv FileZilla.zip "$OUTPUTDIR/$TARGET/FileZilla.zip"

  cd "$OUTPUTDIR/$TARGET" || exit 1
  $SHASUM "FileZilla_3_setup.exe" > "FileZilla_3_setup.exe.sha512" || exit 1
  $SHASUM "FileZilla.zip" > "FileZilla.zip.sha512" || exit 1

elif echo "$TARGET" | grep apple-darwin 2>&1 > /dev/null; then

  cd "$WORKDIR/$PACKAGE"
  do_strip "FileZilla.app/Contents/MacOS" filezilla "$WORKDIR/debug"
  do_strip "FileZilla.app/Contents/MacOS" fzputtygen "$WORKDIR/debug"
  do_strip "FileZilla.app/Contents/MacOS" fzsftp "$WORKDIR/debug"
  do_strip "FileZilla.app/Contents/MacOS" fzstorj "$WORKDIR/debug"

  cd "$WORKDIR/$PACKAGE/FileZilla.app/Contents/Frameworks"
  for i in *.dylib; do
    do_strip "FileZilla.app/Contents/Frameworks" "$i" "$WORKDIR/debug"
  done

  cd "$WORKDIR/$PACKAGE"
  if [ -x "$HOME/prefix-$TARGET/sign.sh" ]; then
    echo "Signing bundle"
    "$HOME/prefix-$TARGET/sign.sh" || exit 1
  fi

  echo "Creating ${PACKAGE}.app.tar.bz2 tarball"
  tar -cjf "$OUTPUTDIR/$TARGET/$PACKAGE.app.tar.bz2" FileZilla.app

  cd "$OUTPUTDIR/$TARGET" || exit 1
  $SHASUM --binary "$PACKAGE.app.tar.bz2" > "$PACKAGE.app.tar.bz2.sha512" || exit 1

else

  cd "$WORKDIR/prefix"

  mkdir -p "$PACKAGE/lib"
  for i in "$PACKAGE"/bin/*; do
    copy_sos `ldd "$i" | grep '=> /home' | sed 's/.*=> //' | sed 's/ .*//'`
    chrpath -r '$ORIGIN/../lib' $i || true
  done
  do_strip "$PACKAGE/bin" filezilla "$WORKDIR/debug"
  do_strip "$PACKAGE/bin" fzputtygen "$WORKDIR/debug"
  do_strip "$PACKAGE/bin" fzsftp "$WORKDIR/debug"
  do_strip "$PACKAGE/bin" fzstorj "$WORKDIR/debug"
  tar -cjf "$OUTPUTDIR/$TARGET/$PACKAGE.tar.bz2" $PACKAGE || exit 1
  cd "$OUTPUTDIR/$TARGET" || exit 1
  $SHASUM --binary "$PACKAGE.tar.bz2" > "$PACKAGE.tar.bz2.sha512" || exit 1

fi

if [ "$STRIP" != "false" ]; then
  echo "Creating debug package"
  cd "$WORKDIR" || exit 1
  tar cjf "$OUTPUTDIR/$TARGET/${PACKAGE}_debug.tar.bz2" "debug" || exit 1
fi
rm -rf "$WORKDIR/debug"

