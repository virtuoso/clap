#GL_INCLUDE = -I/usr/X11R6/include
#GL_LIB = -L/usr/X11R6/lib

ODE := ../ode/build-linux/ode.pc
CC := gcc
LD := $(CC)
PLAT_OBJS := display-glfw.o
CFLAGS := $(CFLAGS) $(GL_INCLUDE) $(shell pkg-config --cflags freetype2) $(shell pkg-config --cflags glfw3)
LDFLAGS := $(GL_LIB) -lGL -lGLEW -lglfw -lpng $(shell pkg-config --libs freetype2)
LDFLAGS += $(shell pkg-config --libs glfw3) -lopenal $(shell pkg-config --libs vorbisfile)
#LDFLAGS += $(shell pkg-config --libs $(ODE))
LDFLAGS += ../ode/build-linux-debug/libode.a -lpthread -lstdc++
#LDFLAGS += ../ode/build-linux/libode.a

