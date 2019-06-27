#! /bin/sh

# This script updates the nightly information for the updatecheck script.

fixupdatecheck()
{
  echo "Updating information for the automated update checks"

  local LATEST="$HOME/output/nightlies/latest.php"
  local WWWDIR="https://filezilla-project.org/nightlies"

  local UPDATER_SIGN="$HOME/updater_sign/updater_sign"

  cd "$OUTPUTDIR"
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

      SUM=`sha512sum -b "$FILE"`
      SIG=`cat "$FILE" | "${UPDATER_SIGN}" --hash --base64 --tag "$DATE"`
      SUM=${SUM%% *}
      SIZE=`stat -c%s "$FILE"`

      if ! [ -f "$LATEST" ]; then
        echo '<?php' > $LATEST.new
        echo "\$nightlies = array();" >> $LATEST.new
      else
        cat "$LATEST" | grep -v "$TARGET" | grep -v "?>" > "$LATEST.new"
      fi
      echo "\$nightlies['$TARGET'] = array();" >> $LATEST.new
      echo "\$nightlies['$TARGET']['date'] = '$DATE';" >> $LATEST.new
      echo "\$nightlies['$TARGET']['file'] = '$WWWDIR/$DATE/$TARGET/$FILE';" >> $LATEST.new
      echo "\$nightlies['$TARGET']['sha512'] = '$SUM';" >> $LATEST.new
      echo "\$nightlies['$TARGET']['size'] = '$SIZE';" >> $LATEST.new
      echo "\$nightlies['$TARGET']['sig'] = '$SIG';" >> $LATEST.new
      echo "?>" >> $LATEST.new
      mv $LATEST.new $LATEST
    done
  done
}

fixupdatecheck

