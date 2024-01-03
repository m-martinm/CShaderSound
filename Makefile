SOURCES = main.c
INCLUDES = -Iraylib/raylib-5.0/src -Iraylib/raygui-4.0/src
LIBS = -L$(CURDIR)/raylib/raylib-5.0/src -lraylib -lopengl32 -lgdi32 -lwinmm -lm
FLAGS = -std=c99

c_shader_sound: $(SOURCES)
	gcc $(SOURCES) $(INCLUDES) -$(FLAGS) -o c_shader_sound.exe $(LIBS)