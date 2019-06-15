#! /bin/sh

# Prebuild script for nightlygen

# This function updates the locales page

. "$SCRIPTS/util.sh"

updatelocales()
{
  local PACKAGE="$1"
  local POT="$2.pot"
  local WWWLOCALES="$HOME/output/locales/$PACKAGE"

  mkdir -p "$WWWLOCALES"

  echo "Updating locales page"

  cd "$WORKDIR/$PACKAGE/locales" >> $LOG 2>&1 || return 1
  nice make -j`cpu_count` >> $LOG 2>&1 || return 1

  echo "Copying locales"

  # Copy pot template
  cp "$POT" "$WWWLOCALES/$POT" || return 1
  chmod 664 "$WWWLOCALES/$POT"

  rm -f $WWWLOCALES/stats~

  local total=
  for i in *.po.new; do

    FILE=${i%%.*}
    PO=${i%.new}

    cp $i $WWWLOCALES/$PO
    chmod 664 $WWWLOCALES/$PO
    cp $FILE.mo $WWWLOCALES/
    chmod 665 $WWWLOCALES/$FILE.mo

    cp $i $i~

    cat >> $i~ << "EOF"

msgid "foobar1"
msgstr ""

msgid "foobar2"
msgstr "foobar2"

#, fuzzy
msgid "foobar3"
msgstr "foobar3"
EOF

    RES=`msgfmt $i~ -o /dev/null --statistics 2>&1`

    TR=$[`echo $RES | sed -e 's/^[^0-9]*\([0-9]\+\)[^0-9]\+\([0-9]\+\)[^0-9]\+\([0-9]\+\)[^0-9]\+/\1/'` - 1]
    FZ=$[`echo $RES | sed -e 's/^[^0-9]*\([0-9]\+\)[^0-9]\+\([0-9]\+\)[^0-9]\+\([0-9]\+\)[^0-9]\+/\2/'` - 1]
    UT=$[`echo $RES | sed -e 's/^[^0-9]*\([0-9]\+\)[^0-9]\+\([0-9]\+\)[^0-9]\+\([0-9]\+\)[^0-9]\+/\3/'` - 1]

    if [ -z "$total" ]; then
      total=$((TR+FZ+UT))
      echo "$total" >> $WWWLOCALES/stats~
    fi

    rm $i~

    LANG=${FILE%_*}
    COUNTRY=${FILE#*_}

    LANGNAME=`cat "$DATADIR/languagecodes" 2>/dev/null | grep "^$LANG "`
    if [ $? ]; then
      LANGNAME=${LANGNAME#* }
    else
      LANGNAME=
    fi

    COUNTRYNAME=
    if [ ! -z "$COUNTRY" ]; then
      COUNTRYNAME=`cat "$DATADIR/countrycodes" 2>/dev/null | grep "^$COUNTRY "`
      if [ $? ]; then
        COUNTRYNAME=${COUNTRYNAME#* }
      fi
    fi

    NAME=
    if [ ! -z "$LANGNAME" ]; then
      NAME="$LANGNAME "
    fi

    if [ ! -z "$COUNTRYNAME" ]; then
      NAME="$NAME($COUNTRYNAME) "
    fi

    echo "$FILE $TR $FZ $UT $NAME" >> $WWWLOCALES/stats~

  done

  chmod 664 $WWWLOCALES/stats~
  mv $WWWLOCALES/stats~ $WWWLOCALES/stats
}

updatelocales lfz libfilezilla
updatelocales fz filezilla

