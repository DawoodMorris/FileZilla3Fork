#! /bin/sh

clientpostbuild()
{
  [ -f "$SCRIPTS/postbuild/postbuild" ] || return 0

  load_file "$SCRIPTS/postbuild/postbuild" || return 1

  while read_file; do

    if [ -z "$REPLY" ]; then continue; fi

    local script="$SCRIPTS/postbuild/$REPLY"
    if ! [ -x "$script" ]; then
      echo "$script not found or not executable"
      continue
    fi

    "$script" $@ || return 1

  done
}

