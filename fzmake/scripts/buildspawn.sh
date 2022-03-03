#! /bin/sh

export SSH="ssh -oPreferredAuthentications=publickey -oStrictHostKeyChecking=yes -oBatchMode=yes -q"
export SCP="scp -o PreferredAuthentications=publickey -o StrictHostKeyChecking=yes -o BatchMode=yes -q -B -C"

failure()
{
  ENDSECONDS=`date '+%s'`
  local span=$((ENDSECONDS - STARTSECONDS))
  echo "Build time: $span seconds" >> "$TARGETLOG"
  touch "$OUTPUTDIR/$FTARGET/failed"
  rm -f "$OUTPUTDIR/$FTARGET/running"
  rm -f "$OUTPUTDIR/$FTARGET/pending"
  rm -f "$WORKDIR/output-$FTARGET.tar"
  rm -rf "$WORKDIR/$FTARGET"
  return 1
}

all_failure()
{
  logprint "$TARGETS: Failed"
  for TARGET in $TARGETS; do
    FTARGET="${TARGET##*/}"
    TARGETLOG="$OUTPUTDIR/$FTARGET/build.log"
    echo -e "FileZilla 3 build log" >> $TARGETLOG
    echo -e "---------------------\n" >> $TARGETLOG
    echo -e "Target: $FTARGET" >> $TARGETLOG
    START=`date "+%Y-%m-%d %H:%M:%S"`
    echo -e "Build started: $START\n" >> $TARGETLOG

    echo -e "Failed to upload required files" >> $TARGETLOG

    touch "$OUTPUTDIR/$FTARGET/failed"
    rm -f "$OUTPUTDIR/$FTARGET/running"
    rm -f "$OUTPUTDIR/$FTARGET/pending"
    rm -f "$WORKDIR/output-*.tar"
    rm -rf "$WORKDIR/$FTARGET"
  done

  spawn_cleanup

  return 1
}

spawn_cleanup()
{
  if [ "$CLEANUP_DONE" = "true" ]; then
    return 1;
  fi

  export CLEANUP_DONE=true
  logprint "$TARGETS: Performing cleanup"
  filter $SSH -i "$KEYFILE" -p $PORT "$HOST" ". /etc/profile; cd $HOSTPREFIX; rm -rf clientscripts work output output.tar clientscripts.tar.bz2;" || all_failure || return 1
}

buildspawn()
{
  ID=$1
  HOST=$2
  HOSTPREFIX=$3
  TARGETS=$4

  echo "buildspawn: ID=$ID, HOST=$HOST, HOSTPREFIX=$HOSTPREFIX TARGETS=$TARGETS"

  CLEANUP_DONE=false

  PORT=${HOST#*:}
  if [ "$HOST" = "$PORT" ]; then
    PORT=22
  else
    HOST=${HOST%:*}
  fi

  logprint "$TARGETS: Uploading packages"

  filter $SSH -i "$KEYFILE" -p $PORT "$HOST" ". /etc/profile; mkdir -p '$HOSTPREFIX/packages'" 2>&1 || all_failure || return 1

  filter rsync -e "$SSH -p$PORT" -a --delete $WORKDIR/source/ $HOST:$HOSTPREFIX/packages 2>&1 || all_failure || return 1

  logprint "$TARGETS: Uploading clientscripts"
  filter $SCP -i "$KEYFILE" -P $PORT "$WORKDIR/clientscripts.tar.bz2" "$HOST:$HOSTPREFIX" || all_failure || return 1

  logprint "$TARGETS: Unpacking clientscripts"
  filter $SSH -i "$KEYFILE" -p $PORT "$HOST" ". /etc/profile; cd $HOSTPREFIX; rm -rf clientscripts; tar -xjf clientscripts.tar.bz2;" 2>&1 || all_failure || return 1

  if [ -f "$CONFDIR/clientpostbuild" ]; then
    filter $SCP -i "$KEYFILE" -P $PORT "$CONFDIR/clientpostbuild" "$HOST:$HOSTPREFIX/clientscripts/postbuild/postbuild" || all_failure || return 1
  fi

  logprint "$TARGETS: Building targets, check target specific logs for details"

  for i in $TARGETS; do
    export TARGET=$i
    export FTARGET="${TARGET##*/}"
    export TARGETLOG="$OUTPUTDIR/$FTARGET/build.log"
    targetlogprint "FileZilla 3 build log"
    targetlogprint "---------------------\n"
    targetlogprint "Target: $TARGET";
    START=`date "+%Y-%m-%d %H:%M:%S"`
    export STARTSECONDS=`date '+%s'`
    targetlogprint "Build started: $START\n"

    touch "$OUTPUTDIR/$FTARGET/running"
    rm "$OUTPUTDIR/$FTARGET/pending"

    targetlogprint "Invoking remote build script"
    if [ -z "$SUBHOST" ]; then
      filter $SSH -i "$KEYFILE" -p $PORT "$HOST" ". /etc/profile; cd $HOSTPREFIX; clientscripts/build.sh \"$HOSTPREFIX\" \"$TARGET\"" >> $TARGETLOG || failure || continue
    else
      filter $SSH -i "$KEYFILE" -p $PORT "$HOST" "ssh $SUBHOST '. /etc/profile; cd $HOSTPREFIX; clientscripts/build.sh \"$HOSTPREFIX\" \"$TARGET\"'" >> $TARGETLOG || failure || continue
    fi

    BUILDENDSECONDS=`date '+%s'`
    local span=$((BUILDENDSECONDS - STARTSECONDS))
    echo "Build time: $span seconds" >> "$TARGETLOG"

    targetlogprint "Downloading built files"
    filter $SCP -i "$KEYFILE" -P $PORT "$HOST:$HOSTPREFIX/output.tar" "$WORKDIR/output-$FTARGET.tar" >> $TARGETLOG || failure || continue

    cd "$WORKDIR"
    tar -xf "$WORKDIR/output-$FTARGET.tar" >> $TARGETLOG 2>&1 || failure || continue

    if [ ! -d "$FTARGET" ]; then
      targetlogprint "Downloaded file does not contain target specific files"
      failure || continue
    fi

    cd "$FTARGET"
    cp * "$OUTPUTDIR/$FTARGET"

    cd "$WORKDIR"
    rm -r "$FTARGET"
    rm "output-$FTARGET.tar";

    touch "$OUTPUTDIR/$FTARGET/successful"
    rm "$OUTPUTDIR/$FTARGET/running"

    targetlogprint "Build successful"

    ENDSECONDS=`date '+%s'`
    local span=$((ENDSECONDS - BUILDENDSECONDS))
    echo "Download time: $span seconds" >> "$TARGETLOG"

    local span=$((ENDSECONDS - STARTSECONDS))
    echo "Total time: $span seconds" >> "$TARGETLOG"

  done

  spawn_cleanup
}

