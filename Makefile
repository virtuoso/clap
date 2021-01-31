BUILDROOT ?= build
BUILDDIR := $(BUILDROOT)/bin
LIBDIR := $(BUILDROOT)/lib
WWWDIR := $(BUILDROOT)/www
OBJDIR := $(BUILDROOT)/obj
TARGETS := bin www
OS := $(shell uname)
#DEBUG := 1

TEST := $(BUILDDIR)/test
LIB := $(LIBDIR)/libclap.a
BIN := $(BUILDDIR)/onehandclap
SERVER := $(BUILDDIR)/server
LDFLAGS_SERVER := -lm
ifneq ($(DEBUG),)
CFLAGS += -ggdb
DBGSTR := "DEBUG"
else
CFLAGS += -ggdb
endif
CFLAGS := $(CFLAGS) -O2 -Wall -DBUILDDATE="\"$(shell date +%Y%m%d_%H%M%S)$(DBGSTR)\""
CFLAGS_SERVER := $(CFLAGS) -DSERVER_STANDALONE=1
# See comments about ftrace -finstrument-functions

# ifneq ($(EMSDK),)
# BUILD := www
# endif
BUILD ?= default
ALL_TARGETS := $(TEST) $(LIB) $(BIN) $(SERVER) run
include $(BUILD).make

is_clang := $(shell :|$(CC) -dM -E -|grep -w __clang__)
is_apple := $(shell :|$(CC) -dM -E -|grep -w __APPLE__)

ifneq ($(DEBUG),)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
CFLAGS_SERVER += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS := -lasan $(LDFLAGS)
LDFLAGS_SERVER := -lasan
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
CFLAGS_SERVER := $(CFLAGS_SERVER) -I$(OBJDIR)
LDFLAGS := $(LDFLAGS) -lm
OBJS := object.o ref.o util.o logger.o graphics.o input.o messagebus.o
OBJS += matrix.o model.o shader.o objfile.o librarian.o json.o clap.o
OBJS += terrain.o ui.o scene.o font.o sound.o networking.o pngloader.o
OBJS += physics.o ui-animations.o input-fuzzer.o $(PLAT_OBJS)
TEST_OBJS := $(OBJS) test.o
SERVER_OBJS := networking_server.o clap.o input.o util.o logger.o ref.o object.o
SERVER_OBJS += librarian.o messagebus.o json.o input-fuzzer.o
LIB_OBJS := $(OBJS)
CLASSER_OBJS := $(OBJS) classer.o
BIN_OBJS := onehandclap.o $(OBJS)
LIB_OBJS := $(patsubst %,$(OBJDIR)/%,$(LIB_OBJS))
BIN_OBJS := $(patsubst %,$(OBJDIR)/%,$(BIN_OBJS))
TEST_OBJS := $(patsubst %,$(OBJDIR)/%,$(TEST_OBJS))
SERVER_OBJS := $(patsubst %,$(OBJDIR)/%,$(SERVER_OBJS))
CLASSER_OBJS := $(patsubst %,$(OBJDIR)/%,$(CLASSER_OBJS))
#SERVER
HEADERS := object.h util.h logger.h common.h clap.h input.h messagebus.h
HEADERS += librarian.h model.h shader.h objfile.h json.h matrix.h primitives.c
HEADERS += terrain.h ui.h scene.h errors.h font.h sound.h physics.h
ASSETS := asset/*
GENERATED := $(OBJDIR)/config.h

.PHONY: run clean all
.SUFFIXES:

all: $(ALL_TARGETS)

$(OBJDIR)/config.h: config.sh
	@test -d $(OBJDIR) || mkdir -p $(OBJDIR)
	@env CC=$(CC) OBJDIR=$(OBJDIR) sh ./config.sh

$(TEST): $(TEST_OBJS)
	@test -d $(BUILDDIR) || mkdir -p $(BUILDDIR)
	@echo "  LD $@"
	@$(LD) $(TEST_OBJS) -o $@ $(LDFLAGS)

$(BIN): $(BIN_OBJS) $(ASSETS)
	@test -d $(BUILDDIR) || mkdir -p $(BUILDDIR)
	@echo "  LD $@"
	@$(LD) $(BIN_OBJS) -o $@ $(LDFLAGS)

$(SERVER): $(SERVER_OBJS)
	@echo "  LD $@"
	@$(LD) $(SERVER_OBJS) -o $@ $(LDFLAGS_SERVER)

$(LIB): $(LIB_OBJS)
	@test -d $(LIBDIR) || mkdir -p $(LIBDIR)
	@echo "  AR $@"
	@$(AR) cru $@ $(LIB_OBJS)

$(OBJDIR)/compile_commands.json: $(TEST_OBJS) $(BIN_OBJS)
	@sh ./gen_compile_commands.sh $(OBJDIR) > $@

run: $(TEST)
	@echo "  TEST"
	@./$(TEST)

clean:
	rm -f $(TEST_OBJS) $(BIN_OBJS) $(BIN) $(TEST) $(GENERATED) $(OBJDIR)/compile_commands.json
	#rm -rf $(OBJ)

$(OBJS): $(HEADERS) $(GENERATED)
$(TEST_OBJS): $(HEADERS) $(GENERATED)
$(LIB_OBJS): $(HEADERS) $(GENERATED)
$(BIN_OBJS): $(HEADERS) $(GENERATED)

$(OBJDIR)/networking_server.o: networking.c
	@echo "$(CC) $(CFLAGS) -c $< -o $@" > $(subst .o,.cmd,$@)
	@echo "  CC $@"
	@$(CC) $(CFLAGS_SERVER) -c $< -o $@

$(OBJDIR)/%.o:
$(OBJDIR)/%.o: %.c
	@echo "$(CC) $(CFLAGS) -c $< -o $@" > $(subst .o,.cmd,$@)
	@echo "  CC $@"
	@$(CC) $(CFLAGS) -c $< -o $@
