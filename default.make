GL_INCLUDE = /usr/X11R6/include
GL_LIB = /usr/X11R6/lib

CC := gcc
LD := $(CC)
PLAT_OBJS := display-glfw.o pngloader.o
CFLAGS := $(CFLAGS) $(GL_INCLUDE) $(shell pkg-config --cflags freetype2) $(shell pkg-config --cflags glfw3)
LDFLAGS := $(GL_LIB) -lGL -lGLEW -lglfw -lpng $(shell pkg-config --libs freetype2)
LDFLAGS += $(shell pkg-config --libs glfw3) -lopenal $(shell pkg-config --libs vorbisfile)
