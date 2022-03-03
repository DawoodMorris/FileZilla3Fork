#! /bin/sh

makerelease()
{
  if [ -z "$RELEASEDIR" ]; then
    RELEASEDIR="$HOME/output/releases"
  fi

  local CONFIGUREIN="$WORKDIR/source/FileZilla3/configure.ac"
  local UPDATER_SIGN="$HOME/updater_sign/updater_sign"

  rm -rf "$RELEASEDIR" > /dev/null 2>&1
  mkdir -p "$RELEASEDIR"
  mkdir -p "$RELEASEDIR/files"
  mkdir -p "$RELEASEDIR/files/debug"

  local version=

  echo "Creating release files"

  exec 4<"$CONFIGUREIN" || return 1
  read <&4 -r version || return 1

  version="${version#*,}"
  version="${version%%,*}"
  version="`echo $version | tr -d ' ,[]'`"

  echo "Version: $version"

  echo "$version" > "$RELEASEDIR/version"
  cp "$WORKDIR/source/FileZilla3/NEWS" "$RELEASEDIR"

  cd "$OUTPUTDIR"

  cp "FileZilla3_snapshot_${SHORTDATE}_src.tar.bz2" "$RELEASEDIR/files/FileZilla_${version}_src.tar.bz2"

  for TARGET in *; do
    if ! [ -f "$OUTPUTDIR/$TARGET/build.log" ]; then
      continue;
    fi
    if ! [ -f "$OUTPUTDIR/$TARGET/successful" ]; then
      continue;
    fi

    cd "$OUTPUTDIR/$TARGET"
    for i in FileZilla*; do
      local ext
      case "$i" in
        *.sha512)
          ext="sha512"
          ;;
        *.app.tar.*)
          ext="app.tar.${i##*.}"
          ;;
        *.tar.*)
          ext="tar.${i##*.}"
          ;;
        *)
          ext="${i##*.}"
          ;;
      esac

      if [ "$ext" = "sha512" ]; then
        continue
      fi
      if [ "$ext" = "sig" ]; then
        continue
      fi

      if echo "$i" | grep debug > /dev/null; then
        local name="FileZilla_${version}_${TARGET}_debug.tar.bz2"

        cp "$i" "${RELEASEDIR}/files/debug/${name}"
        continue;
      fi

      local platform=
      local suffix=
      local infix=
      local hash=1
      local type=
      case "$TARGET" in
        x86_64*mingw*|*win64)
          platform=win64
          if [ "$ext" = "exe" ]; then
            type="-setup"
          fi
          ;;
        i?86*mingw*|*win32)
          platform=win32
          if [ "$ext" = "exe" ]; then
            type="-setup"
          fi
          ;;
        *64-apple*|*86-apple*)
          platform="macosx-x86"
          ;;
        *)
          platform="${TARGET#prefix_}"
          ;;
      esac

      if echo "$i" | grep bundled2 > /dev/null; then
        suffix="_bundled2"
        hash=0
      elif echo "$i" | grep bundled > /dev/null; then
        suffix="_bundled"
        hash=0
      elif echo "$i" | grep sponsored > /dev/null; then
        infix="_sponsored"
        hash=0
      fi

      local name="FileZilla_${version}_${platform}${infix}${type}${suffix}.$ext"

      cp "$i" "${RELEASEDIR}/files/${name}"

      if [ "$hash" = "1" ]; then
        pushd "${RELEASEDIR}/files" > /dev/null
        sha512sum --binary "${name}" >> "FileZilla_${version}.sha512"
        "${UPDATER_SIGN}" --hash --base64 --tag "${version}" "${name}" >> "FileZilla_${version}.sig"
        popd
      fi
    done
  done

}

makerelease
