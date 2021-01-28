CC := $(EMSDK)/upstream/emscripten/emcc
LD := $(CC)
CFLAGS := $(CFLAGS) -s ALLOW_MEMORY_GROWTH=1 -s USE_WEBGL2=1 -s FULL_ES3=1 -s WASM=1
CFLAGS += -s NO_EXIT_RUNTIME=1 -s USE_FREETYPE=1 -s USE_VORBIS=1 -s USE_LIBPNG=1
LDFLAGS := $(LDFLAGS) --shell-file ./shell_clap.html --preload-file ./asset -g4
LDFLAGS += -s USE_FREETYPE=1 -s SAFE_HEAP=1 -s USE_LIBPNG=1
LDFLAGS += -s ALLOW_MEMORY_GROWTH=1 -s WASM=1 -s EXIT_RUNTIME=1 -s USE_WEBGL2=1 -s FULL_ES3=1
LDFLAGS += --no-heap-copy -lopenal -lvorbis -s USE_VORBIS=1 -s ASAN_SHADOW_SIZE=134217728

CFLAGS += -I../ode/include -I../ode/build/include
LDFLAGS += ../ode/build/libode.a

ifneq ($(DEBUG),)
CFLAGS += -ggdb
CFLAGS := $(subst -ggdb,-g4,$(CFLAGS))
#LDFLAGS += -fsanitize=null
LDFLAGS += --source-map-base "http://ukko.local/clap/"
endif

ALL_TARGETS := $(subst run,,$(ALL_TARGETS))
ALL_TARGETS := $(subst $(TEST),$(TEST).html,$(ALL_TARGETS))
ALL_TARGETS := $(subst $(BIN),,$(ALL_TARGETS))
ALL_TARGETS := $(subst $(SERVER),,$(ALL_TARGETS))
ALL_TARGETS := $(subst $(CLASSER),,$(ALL_TARGETS))
PLAT_OBJS := input-www.o display-www.o
OBJDIR := $(BUILDROOT)/objwww
TEST := $(TEST).html
ifneq ($(DESTDIR),)
BIN := $(DESTDIR)/index.html
else
BIN := $(BIN).html
endif
ALL_TARGETS += $(BIN)
