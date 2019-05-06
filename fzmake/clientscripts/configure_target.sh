#! /bin/sh

normalize()
{
  N=`echo "$1" | sed 's/-pc-/-/'`
  N=`echo "$N" | sed 's/-unknown-/-/'`
  echo "$N"
}

BUILD=`"${SCRIPTS:-.}/config.guess"`
COMP=`${CC:-cc} $CFLAGS -dumpmachine`
CONFIGURE_HOST="${CONFIGURE_HOST:-${1:-$BUILD}}"

NHOST=`normalize "$CONFIGURE_HOST"`
NBUILD=`normalize "$BUILD"`
NCOMP=`normalize "$COMP"`

normalize_version()
{
  local HOST="$1"
  local BUILD="$2"

  if echo "$BUILD" | grep -e '^i[1-9]86-' 2>&1 > /dev/null; then
    if echo "$HOST" | grep -e '^i[1-9]86-' 2>&1 > /dev/null; then
      local CPU="${BUILD%%-*}"
      HOST="${CPU}-${HOST#*-}"
    fi
  fi

  if echo "$BUILD" | grep -e '-apple-darwin[0-9.]\+$'  > /dev/null; then
    if echo "$HOST" | grep -e '-apple-darwin[0-9.]\+$'  > /dev/null; then
      local VERSION="${BUILD##*apple-darwin}"
      HOST="${HOST%apple-darwin*}apple-darwin${VERSION}"
    fi
  fi

  echo "$HOST"
}

NHOST=`normalize_version "$NHOST" "$NBUILD"`
NCOMP=`normalize_version "$NCOMP" "$NBUILD"`

if [ "$NHOST" = "$NCOMP" ]; then
  if [ "$NBUILD" != "$NCOMP" ]; then
    echo "Warning: Native compiler ($COMP) does not match build system ($BUILD)" >&2
    echo "--build=$HOST --host=$HOST"
  fi
else
  echo "Configuring with --host=$CONFIGURE_HOST" >&2
  echo "--host=$CONFIGURE_HOST"
fi
