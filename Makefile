# This make file assumes that you have raylib and raygui in the same directory as this file.
# And that you are using Windows with MINGW32.
# If this makefile doesn't work on your system just follow the Makefile in raylib/examples
SOURCES = main.c
INCLUDES = -Iraylib/raylib-5.0/src -Iraylib/raygui-4.0/src
LIBS = -L$(CURDIR)/raylib/raylib-5.0/src -lraylib -lopengl32 -lgdi32 -lwinmm -lm
FLAGS = -std=c99 -Wall -pedantic

c_shader_sound: $(SOURCES)
	gcc $(SOURCES) $(INCLUDES) -$(FLAGS) -o c_shader_sound.exe $(LIBS)

debug : $(SOURCES)
	gcc $(SOURCES) $(INCLUDES) -$(FLAGS) -ggdb -o c_shader_sound_debug.exe $(LIBS)