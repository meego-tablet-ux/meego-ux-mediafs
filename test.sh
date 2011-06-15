#!/bin/bash

mount | grep media-preprocessord > /dev/null
let is_mounted="$?"
if [[ $is_mounted -eq 0 ]]; then
	echo "$0: already mounted, release with \`fusermount -u /tmp/fuse-test/home/Photos'"
	exit 1
fi

if [ ! -d /tmp/fuse-test ]; then
	mkdir -p /tmp/fuse-test/{.photos-hidden,home/{Photos,.thumbnails}}
fi
if [ ! -d /tmp/fuse-test/plugins/readers ]; then
	mkdir -p /tmp/fuse-test/plugins/readers
	cp -a *.so /tmp/fuse-test/plugins/readers
fi

if [ ! -f config ]; then
	cat <<EOF >config
square		ratio=1.00 crop=centre maxwidth_px=128 maxheight_px=128
preview		maxwidth_px=512 maxheight_px=512
EOF
fi

echo "$0: running..."
if test "$GDB"; then
	gdb --args ./media-preprocessord -f -s /tmp/fuse-test/.photos-hidden -m /tmp/fuse-test/home/Photos -t /tmp/fuse-test/home/.thumbnails -c config
else
	test "$VALGRIND" && VALGRIND="valgrind $VALGRIND"
	$VALGRIND ./media-preprocessord -f -s /tmp/fuse-test/.photos-hidden -m /tmp/fuse-test/home/Photos -t /tmp/fuse-test/home/.thumbnails -c config -p /tmp/fuse-test/plugins
fi
