#! /bin/sh

if [ -z "$OLDPATH" ]; then
  export OLDPATH="$PATH"
fi

clean()
{
  unset PREFIX
  unset PREFIXES
  unset LD_LIBRARY_PATH
  unset CPPFLAGS
  unset CFLAGS
  unset CXXFLAGS
  unset LDFLAGS
  export PATH="$OLDPATH"
  unset PKG_CONFIG_PATH
  unset CONFIGURE_ARGS
  unset WINEARCH
  unset WINEPREFIX
}

prepend()
{
  PREFIX="$1"
  if ! [ -d "$PREFIX" ]; then
    if [ -d "prefix-$PREFIX" ]; then
      PREFIX="prefix-$PREFIX"
    elif [ -d "$HOME/prefix-$PREFIX" ]; then
      PREFIX="$HOME/prefix-$PREFIX"
    else
      echo "Invalid prefix $1"
      return 1
    fi
  fi

  if which realpath > /dev/null 2>&1; then
    export PREFIX=`realpath "$PREFIX"`
  else
    if pushd $PREFIX > /dev/null 2>&1; then
      export PREFIX=`pwd`
      popd > /dev/null || return 1
    fi
  fi

  if [ ! -d "$PREFIX" ]; then
    echo "Invalid prefix $1"
    return 1
  fi

  export PREFIXES=$PREFIX${PREFIXES:+ $PREFIXES}

  export PATH="$PREFIX/bin${PATH:+:$PATH}"
  export LD_LIBRARY_PATH="$PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export CPPFLAGS="-I$PREFIX/include${CPPFLAGS:+ $CPPFLAGS}"
  export LDFLAGS="-L$PREFIX/lib${LDFLAGS+ $LDFLAGS}"
  export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

  if [ -x "$PREFIX/profile" ]; then
    . "$PREFIX/profile"
  fi
}

process_in_reverse()
{
  if [ -z $1 ]; then
    return 0
  fi

  local first="$1"
  shift
  if ! process_in_reverse $@; then
    return 1
  fi

  if ! prepend "$first"; then
    return 1
  fi
}

clean

if [ -z "$1" ]; then
  echo "Prefix has been unset"
  return 0
else

  if ! process_in_reverse $@; then
    clean
    return 1
  fi

  export CONFIGURE_ARGS="$CONFIGURE_ARGS --prefix=\"$PREFIX\""

  echo "Prefix set to $PREFIX"
fi

