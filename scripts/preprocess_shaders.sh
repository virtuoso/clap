#!/bin/sh -e

usage() {
    echo "Need -t <type> -o <outdir> <src>"
}

mk_glsl() {
    local src="$1"
    local dest="$2"

    cp "$src" "$dest"
}

mk_glsl_es() {
    local src="$1"
    local dest="$2"

    sed -e 's,#version.*,#version 300 es\nprecision highp float;\n,' \
        < "$src" > "$dest"
}

if test -z "$1"; then
    usage
    exit 1
fi

while [ -n "$1" ]; do
    case "$1" in
        -t)
            shift
            type="$1"
            ;;
        -o)
            shift
            outdir="$1"
            ;;
        *)
            src="$1"
            ;;
    esac
    shift
done

if test -z "$type" -o -z "$outdir" -o -z "$src"; then
    usage
    exit 1
fi

shader=$(basename "$src")
case "$type" in
    glsl)
        mk_glsl "$src.vert" "$outdir/$shader.vert"
        mk_glsl "$src.frag" "$outdir/$shader.frag"
        ;;
    glsl-es)
        mk_glsl_es "$src.vert" "$outdir/$shader.vert"
        mk_glsl_es "$src.frag" "$outdir/$shader.frag"
        ;;
    *)
        exit 1
        ;;
esac
