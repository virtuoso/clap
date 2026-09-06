#!/bin/sh
# Scripted capture smoke test: run a demo with an Umka script that arms the
# capture and shoots <count> frames from <frame> on, counting from the first
# frame rendered for capture, then list what landed in the capture directory.
# The script quits once the writers have every frame; the runtime is the
# backstop for a run that never gets there.
# Usage: capture-test.sh <build-dir> [<frame>[:<count>]] [runtime-seconds]
set -e
CDPATH= cd -- "$(dirname -- "$0")/.."

build="${1:-build/debug}"
spec="${2:-120:3}"
secs="${3:-10}"
# Frames run to megabytes apiece: keep them on disk, in the gitignored
# screenshots/, rather than in a /tmp that may be tmpfs and shared
mkdir -p screenshots
dir="$(mktemp -d "$PWD/screenshots/capture-test.XXXXXX")"

first="${spec%%:*}"
case "$spec" in
    *:*)    count="${spec#*:}" ;;
    *)      count=1 ;;
esac

# The cap on captured frames comes from the spec, not from the settings a
# session in this $HOME saved
mkdir "$dir/scripts"
cat > "$dir/scripts/capture.um" <<EOF
import "clap.um"

var frame: int = 0
var shots: int = 0

fn main() {
    if !clap::set_screenshot_dir("$dir") {
        clap::dbg("capture-test: can't capture into $dir")
        clap::quit()
        return
    }
    clap::set_capture_max_frames($count)
    clap::capture_arm(true)

    clap::on_frame(fn() {
        if !clap::capture_is_armed() {
            return
        }

        if frame >= $first && shots < $count && clap::screenshot() {
            shots++
        }
        frame++

        // quit() lets the writers finish what they have
        if clap::frames_captured() >= $count {
            clap::quit()
        }
    })
}
EOF

# -S: a TEST-NET address that never answers, so no log server takes over
# the log or sends input into the run
env DISPLAY=:0 "$build/demo/ldjam56/clap" -S 192.0.2.1 --scripts "$dir/scripts" -e "$secs" \
    > "$dir/run.log" 2>&1 || echo "exit code: $?"

echo "--- captures in $dir:"
if ls "$dir"/*.exr > /dev/null 2>&1; then ls -l "$dir"/*.exr; else echo "NONE"; fi
exr="$(ls "$dir"/*.exr 2>/dev/null | head -1)"
if [ -n "$exr" ] && command -v ffprobe > /dev/null; then
    echo "--- ffprobe on $(basename "$exr"):"
    ffprobe -v error -show_entries stream=codec_name,width,height,pix_fmt -of csv=p=0 "$exr"
fi
echo "--- capture-related log lines:"
grep -iE "capture|readback|blit|dropping frame" "$dir/run.log" | head -10 || true
echo "logdir $dir"
