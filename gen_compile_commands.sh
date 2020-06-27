#!/bin/sh

srcroot=$PWD
objroot=$PWD

if [ -n "$1" ]; then
    objroot="$1"
fi
cmdroot="$objroot"

if grep -q CONFIG_BROWSER $objroot/config.h; then
    inc="-I../emsdk/upstream/emscripten/system/include" # XXX
fi

echo '['
for f in $(find $cmdroot -name '*.cmd'); do
    #echo $f
    if [ -n "$cmd" ]; then
        echo ","
    fi
    b=$(basename $f)
    dirname=$(dirname $f)
    srcname=$(echo $b | sed -e 's,.cmd$,,').c
    cmd=$(cat $f)
    if [ -n "$inc" ]; then
        cmd=$(echo $cmd|sed -e "s,^.*/emcc,gcc $inc,")
    fi

    echo "{"
    echo "  \"directory\": \"$dirname\","
    echo "  \"command\": \"$cmd\","
    echo "  \"file\": \"$srcname\""
    echo "}"
done
echo ']'
