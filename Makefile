BUILDROOT ?= build
BUILDDIR := $(BUILDROOT)/bin
WWWDIR := $(BUILDROOT)/www
OBJDIR := $(BUILDROOT)/obj
TARGETS := bin www
OS := $(shell uname)
DEBUG := 1

TEST := test
LIB := libclap.a
BIN := onehandclap
CFLAGS := $(CFLAGS) -O2 -Wall
ifneq ($(DEBUG),)
CFLAGS += -ggdb
endif
# See comments about ftrace -finstrument-functions

# ifneq ($(EMSDK),)
# BUILD := www
# endif
BUILD ?= default
ALL_TARGETS := $(TEST) $(LIB) $(BIN) run
include $(BUILD).make

is_clang := $(shell :|$(CC) -dM -E -|grep -w __clang__)
is_apple := $(shell :|$(CC) -dM -E -|grep -w __APPLE__)

ifneq ($(DEBUG),)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS := -lasan $(LDFLAGS)
endif

ifneq ($(call is_apple),)
CFLAGS  += -Wno-deprecated-declarations
LDFLAGS := $(subst -lGL ,-framework OpenGL ,$(LDFLAGS))
LDFLAGS := $(subst -lopenal,-framework OpenAL,$(LDFLAGS))
endif

ifneq ($(call is_clang),)
LDFLAGS := $(subst -lasan,-fsanitize=address,$(LDFLAGS))
LDFLAGS := $(subst -s SAFE_HEAP=1,,$(LDFLAGS))
endif

ALL_TARGETS += $(OBJDIR)/compile_commands.json
#ALL_TARGETS ?= $(TEST) $(LIB) $(BIN) run compile_commands.json

CFLAGS := $(CFLAGS) -I$(OBJDIR)
LDFLAGS := $(LDFLAGS) -lm
OBJS := object.o ref.o util.o logger.o graphics.o input.o messagebus.o
OBJS += matrix.o model.o shader.o objfile.o librarian.o json.o clap.o
OBJS += terrain.o ui.o scene.o font.o $(PLAT_OBJS)
TEST_OBJS := $(OBJS) test.o
LIB_OBJS := $(OBJS)
BIN_OBJS := onehandclap.o $(OBJS)
LIB_OBJS := $(patsubst %,$(OBJDIR)/%,$(LIB_OBJS))
BIN_OBJS := $(patsubst %,$(OBJDIR)/%,$(BIN_OBJS))
TEST_OBJS := $(patsubst %,$(OBJDIR)/%,$(TEST_OBJS))
HEADERS := object.h util.h logger.h common.h clap.h input.h messagebus.h
HEADERS += librarian.h model.h shader.h objfile.h json.h matrix.h primitives.c
HEADERS += terrain.h ui.h scene.h errors.h
ASSETS := asset/*
GENERATED := $(OBJDIR)/config.h

.PHONY: run clean all
.SUFFIXES:

all: $(ALL_TARGETS)

$(OBJDIR)/config.h: config.sh
	test -d $(OBJDIR) || mkdir -p $(OBJDIR)
	env CC=$(CC) OBJDIR=$(OBJDIR) sh ./config.sh

$(TEST): $(TEST_OBJS)
	$(LD) $(TEST_OBJS) -o $@ $(LDFLAGS)

$(BIN): $(BIN_OBJS) $(ASSETS)
	$(LD) $(BIN_OBJS) -o $@ $(LDFLAGS)

$(LIB): $(LIB_OBJS)
	$(AR) cru $@ $(LIB_OBJS)

$(OBJDIR)/compile_commands.json: $(TEST_OBJS) $(BIN_OBJS)
	@sh ./gen_compile_commands.sh $(OBJDIR) > $@

run: $(TEST)
	./$(TEST)

clean:
	rm -f $(TEST_OBJS) $(BIN_OBJS) $(BIN) $(TEST) $(GENERATED) $(OBJDIR)/compile_commands.json
	#rm -rf $(OBJ)

$(OBJS): $(HEADERS) $(GENERATED)
$(TEST_OBJS): $(HEADERS) $(GENERATED)
$(LIB_OBJS): $(HEADERS) $(GENERATED)
$(BIN_OBJS): $(HEADERS) $(GENERATED)

$(OBJDIR)/%.o:
$(OBJDIR)/%.o: %.c
	@echo "$(CC) $(CFLAGS) -c $< -o $@" > $(subst .o,.cmd,$@)
	$(CC) $(CFLAGS) -c $< -o $@
