#!/usr/bin/env bash
debugger=0
if [ "$1" = "-d" ]; then
	debugger=1
	# gdb doesn't like a <<< argument
	shift
fi
gameid=$1; shift
count=$1; shift
if [ -z "$count" ]; then
	echo "Usage: Tools/start-pod.sh [-d] <gameid> <count> path/to/dolphin" >&2
	echo "Starts a pod of netplaying dolphins." >&2
	exit 1
fi
launchtemp=$(mktemp -d /tmp/launchtemp.XXXXXX)
trap "killall -9 Dolphin dolphin-emu; rm -rf $launchtemp; kill -9 0" SIGINT SIGTERM EXIT
launch() {
	dir=$(mktemp -d $launchtemp/XXXXXX)
	mkfifo $dir/fifo
	exec="$1"; shift
	echo "run $@ > $dir/fifo" > $dir/run
	if [ $debugger = 1 ]; then
		[ -z "$gdb" ] && gdb=gdb
		xterm -sl 100000 -e bash -ic "$gdb -x $dir/run $exec" &
		cat "$dir/fifo"
	else
		"$exec" "$@"
	fi
}
launch "$@" -H "$gameid" | {
	seen=0
	while read -r a b c d rest; do
		echo $a $b $c $d $rest
		if [ $seen = 0 -a "$a $b $c" = "Traversal state: Connected" ]; then
			hostid="$d"
			for((i=1;i<$count;i++)); do
				launch "$@" -c "$hostid" &
			done
			seen=1
		fi
	done
}

