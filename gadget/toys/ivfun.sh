#!/bin/sh
#
# ivfun.sh -- animate the photos displayed by image-viewer
#

# Return the XID of image-viewer's animation actors.
getwins()
{
	appid=`map -v image-viewer | grep -o 0x...`;
	for win in `map -Q | grep -B 1 '    Geometry: 800x[0-9]\++800+[0-9]\+$' | sed -ne 's/^ *Subwindow \('$appid'[0-9a-f]\+\):$/\1/p'`;
	do
		if xprop -id "$win" | grep -q "program specified minimum size: 0 by 0$";
		then
			bad="$bad $win";
		else
			good="$good $win";
		fi
	done
	echo $good $bad;
}

# Check that the parent of a subprocess is still running.
anyad=$$;
checkparent()
{
	if ! kill -0 $anyad 2> /dev/null;
	then	# Restore the animation actor.
		map -A anchor -A move -A rotate=0 -A rotate=1 -A show $1;
		exit 0;
	fi
}

# Main starts here
depth1=500;
depth2=300
opacity=200;

[ $# -gt 0 ] || set -- `getwins`;
echo "$@";

win1=$1;
win2=$2;
win3=$3;
[ "$win3" ] || exit 1;
shift 3;

# Create the map parameters.
# $init on every frame.
init="$init -A anchor=9 -A move=480,240,-$depth1"
init="$init -A rotate=0,20";
init="$init -A show=1,$opacity";
for i in `seq 0 359`;
do	# Rotate-animate.
	line1="$line1 $init";
	line1="$line1 -A rotate=1,$((i+58)),0,0,-$depth2";
	line1="$line1 -W 20ms";
	line2="$line2 $init";
	line2="$line2 -A rotate=1,$((i+180)),0,0,-$depth2";
	line2="$line2 -W 20ms";
	line3="$line3 $init";
	line3="$line3 -A rotate=1,$((i+302)),0,0,-$depth2";
	line3="$line3 -W 20ms";
done

# Start the engines.
map -A show=0 "$@";
while :; do map $line1 $win1; checkparent $win1; done & p1=$!;
while :; do map $line2 $win2; checkparent $win2; done & p2=$!;
while :; do map $line3 $win3; checkparent $win3; done & p3=$!;

# This doesn't really work.
trap "echo bye; kill $p1 $p2 $p3; exit 0;" INT;

# While then engines are running check the animation actors periodically,
# and restart ourselves if they change.
while sleep 15;
do
	set -- `getwins`;
	if [ $# -lt 3 ];
	then	# image-viewer has exited.
		kill $p1 $p2 $p3;
		exit;
	elif [ "$1" != "$win1" -o "$2" != "$win2" -o "$3" != "$win3" ];
	then
		kill $p1 $p2 $p3;
		exec sh "$0" "$@";
	fi
done

# End of ivfun.sh
