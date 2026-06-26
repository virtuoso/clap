# Scripting clap with Umka

clap embeds the [Umka](https://github.com/vtereshkov/umka-lang) interpreter. A
script drives the live engine through the `clap` FFI module: move entities and
the camera, position and recolour lights, tweak the post-processing pipeline,
build colour-grading LUTs. It is aimed at fast, no-rebuild iteration — a debug
loop of *poke the scene, screenshot, adjust, screenshot* — and is a first-class
target for AI agents.

This page covers the concepts. For the exact callable surface see the
**generated API reference** (`script-api.md`, produced from the code so it never
drifts — it is deployed next to the web demos, and lands at
`build/<cfg>/core/script/script-api.md` after any build). The source of truth is
the `CLAP_SCRIPT_*` / `FFI_*` markers in `core/script/*.c`.

## The language, in one screen

Umka is small and Go-like. What you need to know to use the `clap` module:

```
import "clap.um"                        // pulls in the FFI module

var frames: int = 0                     // module-level state persists across frames

fn main() {                             // runs once when the script (re)loads
    clap::dbg("loaded")                 // everything is under the clap:: namespace

    e := clap::get_entity3d("gem")      // ':=' declares + infers; handles are values
    if e.valid() {                      // always check a handle before using it
        e.get_xform().rotate_Y(90)      // methods chain
    }

    clap::on_frame(fn() {               // register a per-frame closure
        frames += 1
        if frames % 60 == 0 {
            clap::dbg(sprintf("frame %d", frames))   // sprintf is a builtin
        }
    })
}
```

- **Namespace.** Functions are `clap::name(...)`; types the module defines are
  also qualified — a `Vec3` is `clap::Vec3` (this matters when you *name* the
  type, e.g. a `^clap::Vec3` parameter).
- **Handles** (`Entity3d`, `Camera`, `Xform`, `Light`, `RenderOptions`, ...) are
  opaque values. A default/zeroed handle is invalid; `get_entity3d`/`get_light`
  return an invalid handle when nothing matches, so guard with `.valid()`. A
  handle is only good while the underlying object lives.
- **Vectors.** `Vec3` is `struct { x, y, z: real }`. Getters return one; setters
  take loose components (`set_position(x, y, z)`).
- **Stopping a callback.** `on_frame` returns a `Frame`; its `cancel()` stops
  the callback after the current tick, and is fine to call from anywhere,
  the callback itself included.

### Gotcha: per-frame state goes in a module var

Each deferred `on_frame` call gets a **fresh copy** of the closure's captured
variables, so mutating a capture does not persist. Keep counters and the like in
a module-level `var` (as above), not in `|captures|`.

## Recipes

**Pose the scene** (entities, camera, control):

```
clap::get_entity3d("golem").get_xform().set_position(0, 0, 0)
cam := clap::get_camera()
cam.get_xform().set_position(5, 8, 12)
clap::control(clap::get_entity3d("golem"))   // camera follows the golem
clap::detach()                               // ...or free-fly
```

**Light a shadow-debugging shot:**

```
for i := 0; i < clap::light_count(); i++ {
    l := clap::get_light(i)
    if l.valid() {
        clap::dbg(sprintf("light %d radius %.1f", i, l.radius()))
    }
}
key := clap::get_light(0)
key.set_position(10, 40, 10)
key.set_color(2.0, 1.8, 1.5)         // HDR: values > 1 are fine
clap::set_ambient(0.05, 0.05, 0.08)
clap::set_shadow_tint(0.1, 0.1, 0.2)
```

**Tune the pipeline:**

```
ro := clap::render()
ro.set_shadow_vsm(true)
ro.set_bloom_intensity(3.0)
ro.set_contrast(0.08)
ro.set_aabb_draws(true)              // debug overlay for all entity AABBs
```

**Design a LUT from a colour function.** The grade writes its result through an
out pointer (so it needs no return); inputs and output are linear RGB:

```
fn grade(r, g, b: real, out: ^clap::Vec3) {
    out.x = r * 1.1                  // warm the highlights, cool the shadows
    out.y = g
    out.z = b * 0.9 + 0.02
}

fn main() {
    clap::make_lut("warm", grade)    // reusing a name rebuilds that LUT
    clap::set_lut("warm")            // apply by name
}
```

`make_lut` evaluates `grade` for every cell of a 32³ LUT, so it is a one-shot
design action — call it from `main`, not from `on_frame`.

## Examples

Ready-to-crib scripts live in [`script-examples/`](script-examples/):

- [`on-frame.um`](script-examples/on-frame.um) — per-frame updates and the module-var pattern
- [`pose-scene.um`](script-examples/pose-scene.um) — position entities and the camera
- [`light-debug.um`](script-examples/light-debug.um) — enumerate and drive lights
- [`grade-lut.um`](script-examples/grade-lut.um) — build and apply a colour-grading LUT
