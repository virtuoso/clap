GL_INCLUDE = /usr/X11R6/include
GL_LIB = /usr/X11R6/lib

CC := gcc
LD := $(CC)
PLAT_OBJS := display-glfw.o pngloader.o
CFLAGS := $(CFLAGS) -I$(GL_INCLUDE)
LDFLAGS := -L$(GL_LIB) -lGL -lGLEW -lglfw -lpng
ifneq ($(DEBUG),)
CFLAGS += -fsanitize=address
LDFLAGS := -lasan $(LDFLAGS)
endif
# -lfreetype
