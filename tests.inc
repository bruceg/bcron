export USER=`whoami`
export HOST=`hostname -f`
export BCRON_SPOOL=$tmp/spool
export BCRON_USER=$USER
export BCRONTAB_SOCKET=$tmp/socket
export CRONTABS=$BCRON_SPOOL/crontabs

mkdir $BCRON_SPOOL
mkdir $CRONTABS
mkdir $BCRON_SPOOL/tmp
mkfifo $BCRON_SPOOL/trigger

netstring() {
    len=$( printf "$@" | wc -c )
    fmt="$1"
    shift
    printf "%d:${fmt}," $len "$@"
}

spoolsockcmd() {
  unixclient $BCRONTAB_SOCKET sh -c "
    perl -e '
      \$string = join(\"\", <>);
      printf \"%d:%s,\", length(\$string), \$string;
    ' >&7; cat <&6; echo"
}

spoolcmd() {
    perl -e '
      $string = join("", <>);
      printf "%d:%s,", length($string), $string;
    ' | env PROTO=UNIX UNIXREMOTEEUID=`id -u` $src/bcron-spool "$@" 2>/dev/null
    echo
}

sched() {
  MSG_DEBUG=0 $src/test-sched "$@" 2>&1
}

startsched() {
    sched_pid=$( $src/bcron-sched >$tmp/sched-log 2>&1 & echo $! )
    sleep 1
}

stopsched() {
    sleep 1
    kill $sched_pid

    sed -e '
        s/next:....-..-.. ..:..:.. [^ ]*/next:####-##-## ##:##:## #TZ/;
    ' <$tmp/sched-log
}

catsub() {
    cat -v "$@" \
    | sed -e "
        s/($USER)/(USER)/g
        s/<$USER>/<USER>/g
        s/<$USER@/<USER@/g
	s/@$HOST>/@HOST>/g
    "
}

doexec() {
    num=1
    for cmd in "$@"
    do
	netstring "${num}\\0${USER}\\0${cmd}"
	num=$(($num+1))
    done | MSG_DEBUG=0 TESTMODE=1 $src/bcron-exec >$tmp/exec.out 2>$tmp/exec.err
    catsub $tmp/exec.out; echo
    catsub $tmp/exec.err
}
