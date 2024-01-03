# CShaderSound

CShaderSound is an audio visualizer written in C, utilizing shaders for real-time visualization. It is a small, personal project to try to learn more about shader programming.

## Dependencies
The [Raylib](https://github.com/raysan5/raylib) and [Raygui](https://github.com/raysan5/raygui/releases) library. Follow the instructions on [Raylib GitHub](https://github.com/raysan5/raylib) for installation.

## Usage

You can change some configurations in the __main.c__ file. Then simply compile it. 

```bash
./c_shader_sound.exe <path-to-filename>
```
(Change the file extension based on your system.)

Or simply run the program and drag-and-drop a music file.

## Controls

- Press __SPACE__ to toggle play/pause.
- Press __R__ to reload shaders. (You can use this program kinda like Shadertoys.)
- Drop an audio file onto the window to load it.

---
This is small project has been inspired by [@Tsoding](https://github.com/tsoding/musializer) and [ShaderToys](https://www.shadertoy.com/).
