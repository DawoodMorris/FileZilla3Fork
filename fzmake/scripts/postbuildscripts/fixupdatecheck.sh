#! /bin/sh

# This script updates the nightly information for the updatecheck script.

fixupdatecheck()
{
  echo "Updating information for the automated update checks"

  local LATEST="$HOME/output/nightlies/latest.php"
  local WWWDIR="https://filezilla-project.org/nightlies"

  local UPDATER_SIGN="$HOME/updater_sign/updater_sign"

  cd "$OUTPUTDIR"

  if [ -f "$LATEST" ]; then
    cp "$LATEST" "$WORKDIR/latest.php"
  fi

  for TARGET in *; do
    if ! [ -f "$OUTPUTDIR/$TARGET/build.log" ]; then
      continue;
    fi
    if ! [ -f "$OUTPUTDIR/$TARGET/successful" ]; then
      continue;
    fi

    # Successful build

    cd "$OUTPUTDIR/$TARGET"
    for FILE in *; do
      if [ $FILE = "successful" ]; then
        continue;
      fi
      if [ $FILE = "build.log" ]; then
        continue;
      fi
      if [ ${FILE: -4} = ".zip" ]; then
        continue;
      fi
      if [ ${FILE: -7} = ".sha512" ]; then
        continue;
      fi
      if echo ${FILE} | grep debug > /dev/null 2>&1; then
        continue;
      fi
      if echo ${FILE} | grep bundled > /dev/null 2>&1; then
        continue;
      fi
      if echo ${FILE} | grep sponsored > /dev/null 2>&1; then
        continue;
      fi

      SUM=`sha512sum -b "$FILE"`
      SIG=`cat "$FILE" | "${UPDATER_SIGN}" --hash --base64 --tag "$DATE"`
      SUM=${SUM%% *}
      SIZE=`stat -c%s "$FILE"`

      if ! [ -f "$WORKDIR/latest.php" ]; then
        echo '<?php' > "$WORKDIR/latest.new"
        echo "\$nightlies = array();" >> "$WORKDIR/latest.new"
      else
        cat "$WORKDIR/latest.php" | grep -v "$TARGET" | grep -v "?>" > "$WORKDIR/latest.new"
      fi
      echo "\$nightlies['$TARGET'] = array();" >> "$WORKDIR/latest.new"
      echo "\$nightlies['$TARGET']['date'] = '$DATE';" >> "$WORKDIR/latest.new"
      echo "\$nightlies['$TARGET']['file'] = '$WWWDIR/$DATE/$TARGET/$FILE';" >> "$WORKDIR/latest.new"
      echo "\$nightlies['$TARGET']['sha512'] = '$SUM';" >> "$WORKDIR/latest.new"
      echo "\$nightlies['$TARGET']['size'] = '$SIZE';" >> "$WORKDIR/latest.new"
      echo "\$nightlies['$TARGET']['sig'] = '$SIG';" >> "$WORKDIR/latest.new"
      echo "?>" >> "$WORKDIR/latest.new"
      mv "$WORKDIR/latest.new" "$WORKDIR/latest.php"
    done
  done

  if [ -f "$WORKDIR/latest.php" ]; then
    mv "$WORKDIR/latest.php" "$LATEST"
  fi
}

fixupdatecheck

