/* Some known issues and limitations
 * - BUFFER_SIZE should be scaled with the music sample rate and the fps during runtime
 *   but shader arrays require a fixed length. We could solve this with using a bigger buffer size
 *   and padding the not used slots in the array with zeros, but then we need another uniform
 *   for telling the shader the current usable size of the array/buffer. This would cause I think
 *   unnecessary complexity with little improvement in quality.
 * - I think the fft part is not working on MAC because it doesn't support complex floats
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <complex.h>
#include <string.h>
#include "raylib.h"
#include "raymath.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "style_dark.h"
#include "queue.h"

// "Settings"

#define DEBUG_MODE 0
#define BUFFER_SIZE 512
#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Typedefs (personal preference)

typedef float complex fcplx;
typedef float f32;
typedef unsigned int u32;
typedef int i32;
// Structs

typedef struct ui_struct // Holds information about the UI
{
  Vector2 window_size;
  Shader shader;
  Texture canvas;
  const char *music_name;
  Rectangle canvas_bounds;
  Rectangle music_name_bounds;
  Rectangle stop_play_bounds;
  Rectangle progress_bounds;
  Rectangle skip_bounds;
  Rectangle volume_hover_bounds;
  Rectangle volume_slider_bounds;
  bool volume_hovered;
  Queue music_queue;
} UI;

typedef struct audio_struct
{
  Music music;
  Color *pixel_buffer;
  f32 *music_data; // Maybe not the most memory efficient method but raylib doesnt expose the music
                   // data and audio callbacks are a pain in the...
  u32 current_frame;
  bool audio_loaded;
  i32 audio_flag; // We need this flag to know when an audio is over
  fcplx fft_buffer[BUFFER_SIZE];
  f32 previous_avg; // Needed for smoothing out the fft
} Audio;

typedef struct shader_uniforms_struct
{
  Texture2D u_buffer;
  f32 u_time;
  Vector2 u_resolution;

  i32 u_buffer_loc;
  i32 u_time_loc;
  i32 u_resolution_loc;

} ShaderUniforms;

// Some MACROS

#define c2dB(x) 20.0 * log10f(cabsf(x)) // Complex number to dB
#define f2dB(x) 20.0 * log10f(fabsf(x)) // Float to dB
#define DegToRad(x) (PI * (x) / 180.0)  // Convert degrees to radians
#define RadToDeg(x) (180.0 * (x) / PI)  // Convert radians to degrees
#define DEBUG_SEGFAULT printf("Passed line : %d\n", __LINE__);

// Module variables so I dont have to pass every struct around

static Audio audio;
static UI ui;
static ShaderUniforms shader_uniforms;

// Module functions

static void load_audio(const char *file_path);
static f32 *load_wave_frames();
static void check_dropped_files();
static void send_shader_uniforms();
static void ui_draw();
static void toggle_music_playing();
static void resize_window();
static void check_dropped_files();
static void reload_shader();
static void load_audio_buffers();
static void fft();
static void fft_postprocess();

int main(int argc, char **argv)
{
  // Initializing Raylib
  const u32 width = 75 * 16;
  const u32 height = 75 * 9;
#if (DEBUG_MODE == 0)
  SetTraceLogLevel(LOG_ERROR | LOG_FATAL | LOG_WARNING);
#endif
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT); 
  InitWindow(width, height, "CShaderSound");
  SetWindowMinSize(640, 480);
  SetWindowIcon(LoadImage("icon.png"));
  InitAudioDevice();
  SetMasterVolume(0.5f);
  SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));
  GuiLoadStyleDark();
  Font font = LoadFontEx("anita_semi_square.ttf", 28, NULL, 0);
  GuiSetFont(font);
  GuiSetStyle(DEFAULT, TEXT_SIZE, 28);
  GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER); 
  GuiSetIconScale(2);

  // Initializing the UI struct
  queue_init(&ui.music_queue);
  ui.window_size = (Vector2){.x = (f32)width, .y = (f32)height};
  ui.canvas_bounds = (Rectangle){.x = 0, .y = 0, .width = ui.window_size.x, .height = ui.window_size.y * 0.8};
  ui.music_name_bounds = (Rectangle){.x = 0, .y = ui.window_size.y * 0.8, .width = ui.window_size.x, .height = ui.window_size.y * 0.1};
  ui.stop_play_bounds = (Rectangle){.x = 0, .y = ui.window_size.y * 0.9, .width = 0.1 * ui.window_size.x, .height = ui.window_size.y * 0.1};
  ui.progress_bounds = (Rectangle){.x = 0.2 * ui.window_size.x, .y = ui.window_size.y * 0.9, .width = ui.window_size.x * 0.8, .height = ui.window_size.y * 0.1};
  ui.skip_bounds = (Rectangle){.x = 0.1 * ui.window_size.x, .y = ui.window_size.y * 0.9, .width = ui.window_size.x * 0.1, .height = ui.window_size.y * 0.1};
  ui.volume_hover_bounds = (Rectangle){.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0};
  ui.volume_slider_bounds = (Rectangle){.x = 10.0, .y = 10.0, .width = 200.0, .height = 50.0};
  Image tmp = GenImageColor(ui.canvas_bounds.width, ui.canvas_bounds.height, BLANK);
  ui.canvas = LoadTextureFromImage(tmp);
  UnloadImage(tmp);

  ui.shader = LoadShader(0, "shaders/test.frag");

  /*
    uniform vec2 uResolution;
    uniform float uTime;
    uniform float uBuffer[BUFFER_SIZE];
  */
  shader_uniforms.u_resolution_loc = GetShaderLocation(ui.shader, "uResolution");
  shader_uniforms.u_time_loc = GetShaderLocation(ui.shader, "uTime");
  shader_uniforms.u_buffer_loc = GetShaderLocation(ui.shader, "uBuffer");

  // Initializing the ShaderUniorms struct
  shader_uniforms.u_time = 0.0f;
  Image temp = GenImageColor(BUFFER_SIZE, 1, BLANK); // 512 is the buffer length and height 1: .r/.x is the fft and .g/.y is the amplitude
  shader_uniforms.u_buffer = LoadTextureFromImage(temp);
  SetTextureFilter(shader_uniforms.u_buffer, TEXTURE_FILTER_BILINEAR); // Linear filtering
  SetTextureWrap(shader_uniforms.u_buffer, TEXTURE_WRAP_CLAMP);
  UnloadImage(temp);
  shader_uniforms.u_resolution = (Vector2){.x = ui.canvas_bounds.width, .y = ui.canvas_bounds.height};

  // Initializing the audio struct and parsing if a filename was provided
  audio.pixel_buffer = (Color *)malloc(sizeof(Color) * BUFFER_SIZE);
  for (i32 i = 0; i < BUFFER_SIZE; ++i) // Maybe also memset() would work not sure
  {
    audio.fft_buffer[i] = 0.0f + 0.0f * I;
  }
  audio.previous_avg = 0.0f;
#if DEBUG_MODE
  load_audio("songs/lens.mp3");
#endif

  // Main loop
  while (!WindowShouldClose())
  {

    check_dropped_files();

    if (IsKeyPressed(KEY_R))
    {
      reload_shader();
    }

    if (IsKeyPressed(KEY_SPACE))
    {
      toggle_music_playing();
    }

    Vector2 m_pos = GetMousePosition();
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) // Sliding the music stream (Not using the sliders value because it's messier that way)
    {
      if (CheckCollisionPointRec(m_pos, ui.progress_bounds))
      {
        f32 pos_in_secs = Remap(m_pos.x - ui.progress_bounds.x, 0.0f, ui.progress_bounds.width, 0.0f, GetMusicTimeLength(audio.music));
        SeekMusicStream(audio.music, pos_in_secs);
      }
    }

    if (IsWindowResized())
    {
      resize_window();
    }

    if (audio.audio_loaded) // If we have a loaded music file
    {
      // volume control
      if (CheckCollisionPointRec(m_pos, ui.volume_hover_bounds) && !ui.volume_hovered)
      {
        ui.volume_hovered = true;
      }
      else if (!CheckCollisionPointRec(m_pos, ui.volume_slider_bounds) && ui.volume_hovered)
      {
        ui.volume_hovered = false;
      }

      UpdateMusicStream(audio.music);
      audio.current_frame = (int)(GetMusicTimePlayed(audio.music) * audio.music.stream.sampleRate);

      // queueing music
      if (audio.current_frame == 0 && audio.audio_flag >= 2)
      {

        if (!queue_is_empty(&ui.music_queue))
        {
          char *data = dequeue(&ui.music_queue);
          load_audio(data);
          free(data);
        }
        else // if there is no music on queue replay music
        {
          PlayMusicStream(audio.music);
        }
      }
      else if (audio.current_frame == 0 && audio.audio_flag < 2)
      {
        audio.audio_flag++;
      }

      load_audio_buffers();
      fft();
      fft_postprocess();

      BeginDrawing();
      ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
      BeginShaderMode(ui.shader);
      send_shader_uniforms(); // We send them here outherwise the sampler2D would be reset
      // We flip the coordinates
      DrawTextureRec(ui.canvas, (Rectangle){.x = 0.f, .y = 0.f, .width = ui.canvas_bounds.width, .height = -ui.canvas_bounds.height}, (Vector2){.x = ui.canvas_bounds.x, .y = ui.canvas_bounds.y}, WHITE);
      EndShaderMode();
      ui_draw();
      EndDrawing();
    }
    else
    {
      BeginDrawing();
      ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
      GuiDrawText("To start playing a music,\nsimply drop the file onto this window!\n", ui.canvas_bounds, TEXT_ALIGN_MIDDLE, RAYWHITE);
      EndDrawing();
    }
  }

  UnloadMusicStream(audio.music);
  UnloadFont(font);
  free(audio.music_data);
  free(audio.pixel_buffer);
  UnloadTexture(ui.canvas);
  UnloadTexture(shader_uniforms.u_buffer);
  queue_destroy(&ui.music_queue);
  CloseAudioDevice();
  CloseWindow();
  return 0;
}

void reload_shader()
{
  UnloadShader(ui.shader);
  ui.shader = LoadShader(0, "shaders/test.frag");
  shader_uniforms.u_buffer_loc = GetShaderLocation(ui.shader, "uBuffer");
  shader_uniforms.u_resolution_loc = GetShaderLocation(ui.shader, "uResolution");
  shader_uniforms.u_time_loc = GetShaderLocation(ui.shader, "uTime");
}

void resize_window()
{
  ui.window_size.x = (f32)GetRenderWidth();
  ui.window_size.y = (f32)GetRenderHeight();
  ui.canvas_bounds = (Rectangle){.x = 0, .y = 0, .width = ui.window_size.x, .height = ui.window_size.y * 0.8};
  ui.music_name_bounds = (Rectangle){.x = 0, .y = ui.window_size.y * 0.8, .width = ui.window_size.x, .height = ui.window_size.y * 0.1};
  ui.stop_play_bounds = (Rectangle){.x = 0, .y = ui.window_size.y * 0.9, .width = 0.1 * ui.window_size.x, .height = ui.window_size.y * 0.1};
  ui.progress_bounds = (Rectangle){.x = 0.2 * ui.window_size.x, .y = ui.window_size.y * 0.9, .width = ui.window_size.x * 0.8, .height = ui.window_size.y * 0.1};
  ui.skip_bounds = (Rectangle){.x = 0.1 * ui.window_size.x, .y = ui.window_size.y * 0.9, .width = ui.window_size.x * 0.1, .height = ui.window_size.y * 0.1};
  UnloadTexture(ui.canvas);
  Image tmp = GenImageColor(ui.canvas_bounds.width, ui.canvas_bounds.height, BLANK);
  ui.canvas = LoadTextureFromImage(tmp);
  UnloadImage(tmp);
  shader_uniforms.u_resolution = (Vector2){.x = ui.canvas_bounds.width, .y = ui.canvas_bounds.height};
}

// Wrapper around LoadMusicStream()
// NOTE: No error checking yet (TODO)
void load_audio(const char *file_path)
{
  audio.audio_loaded = false;
  ui.music_name = GetFileNameWithoutExt(file_path);
  if (IsMusicReady(audio.music))
  {
    UnloadMusicStream(audio.music);
  }
  if (audio.music_data != NULL)
  {
    free(audio.music_data);
  }
  audio.music = LoadMusicStream(file_path);
  audio.music.looping = false;

  Wave tmp = LoadWave(file_path);
  audio.music_data = load_wave_frames(tmp);
  UnloadWave(tmp);
  while (!IsMusicReady(audio.music))
  { // Wait for music to be initialized
  }
  audio.audio_loaded = true;
  audio.audio_flag = 0;
  audio.current_frame = 0;
  PlayMusicStream(audio.music);
}

// Wrapper around LoadWaveSamples()
f32 *load_wave_frames(Wave w)
{
  f32 *tmp = LoadWaveSamples(w);
  f32 *ret = (f32 *)malloc(sizeof(f32) * w.frameCount);
  for (u32 i = 0; i < w.frameCount; i++)
  {
    f32 amp = 0.0f;
    for (u32 ch = 0; ch < w.channels; ch++)
    {
      amp += tmp[w.channels * i + ch] / w.channels;
    }
    ret[i] = Remap(amp, -1.0f, 1.0f, 0.0f, 1.0f); // Remapping the values to be consistent with the fft
  }
  UnloadWaveSamples(tmp);
  return ret;
}

void send_shader_uniforms()
{
  shader_uniforms.u_time = (f32)GetTime(); // Maybe should be done somewhere else
  SetShaderValue(ui.shader, shader_uniforms.u_time_loc, &(shader_uniforms.u_time), SHADER_UNIFORM_FLOAT);
  SetShaderValue(ui.shader, shader_uniforms.u_resolution_loc, &(shader_uniforms.u_resolution), SHADER_UNIFORM_VEC2);
  SetShaderValueTexture(ui.shader, shader_uniforms.u_buffer_loc, shader_uniforms.u_buffer); // Maybe we can set it in send_shader_uniforms()
}

void ui_draw()
{
  GuiLabel(ui.music_name_bounds, ui.music_name);
  const char *bt_text = TextFormat("#%d#", IsMusicStreamPlaying(audio.music) ? ICON_PLAYER_PAUSE : ICON_PLAYER_PLAY);
  if (GuiButton(ui.stop_play_bounds, bt_text))
  {
    toggle_music_playing();
  }
  if (GuiButton(ui.skip_bounds, GuiIconText(ICON_PLAYER_NEXT, "")))
  {
    if (!queue_is_empty(&ui.music_queue))
    {

      char *data = dequeue(&ui.music_queue);
      load_audio(data);
      free(data);
    }
    else
    {
      fprintf(stderr, "Couldn't skip song, because the queue is empty!\n");
    }
  }
  f32 progress = GetMusicTimePlayed(audio.music);
  GuiSlider(ui.progress_bounds, NULL, NULL, &progress, 0.0f, GetMusicTimeLength(audio.music));
  if (!ui.volume_hovered)
  {
    GuiButton(ui.volume_hover_bounds, GuiIconText(ICON_AUDIO, ""));
  }
  else
  {
    f32 vol = GetMasterVolume();
    GuiSlider(ui.volume_slider_bounds, NULL, NULL, &vol, 0.0f, 1.0f);
    SetMasterVolume(vol);
  }

#if DEBUG_MODE
  DrawRectangleLinesEx(ui.canvas_bounds, 1.0f, YELLOW);
  DrawRectangleLinesEx(ui.music_name_bounds, 1.0f, YELLOW);
  DrawRectangleLinesEx(ui.stop_play_bounds, 1.0f, YELLOW);
  DrawRectangleLinesEx(ui.progress_bounds, 1.0f, YELLOW);
  DrawRectangleLinesEx(ui.skip_bounds, 1.0f, YELLOW);
  DrawRectangleLinesEx(ui.volume_hover_bounds, 1.0f, YELLOW);
#endif
}

void toggle_music_playing()
{
  if (IsMusicStreamPlaying(audio.music))
    PauseMusicStream(audio.music);
  else
    ResumeMusicStream(audio.music);
}

// Checks if files are dropped and loads the first one if there is no music playing.
void check_dropped_files()
{
  if (IsFileDropped())
  {
    FilePathList fp = LoadDroppedFiles();
    for (i32 i = 0; i < fp.count; i++)
    {
      enqueue(&ui.music_queue, fp.paths[i]);
    }
    queue_print(&ui.music_queue);
    if (!IsMusicReady(audio.music) && !queue_is_empty(&ui.music_queue))
    {
      char *data = dequeue(&ui.music_queue);
      load_audio(data);
      free(data);
    }
    UnloadDroppedFiles(fp);
  }
}

void load_audio_buffers()
{
  u32 n;
  u32 first_frame;
  if (audio.current_frame + BUFFER_SIZE > audio.music.frameCount)
  {
    n = audio.music.frameCount;
    first_frame = n - BUFFER_SIZE;
  }
  else
  {
    first_frame = audio.current_frame;
    n = audio.current_frame + BUFFER_SIZE;
  }
  i32 cnt = 0;
  for (u32 i = first_frame; i < n; i++)
  {
    audio.fft_buffer[cnt] = audio.music_data[i] + 0.0f * I;
    unsigned char val = (unsigned char)Remap(audio.music_data[i], 0.0, 1.0, 0.0, 255.0);
    audio.pixel_buffer[cnt] = (Color){.r = (unsigned char)0, .g = val, .b = (unsigned char)0, .a = (unsigned char)0};
    cnt++;
  }
}

void _fft(fcplx *buf, fcplx *out, u32 n, u32 step)
{
  if (step < n)
  {
    _fft(out, buf, n, step * 2);
    _fft(out + step, buf + step, n, step * 2);

    for (i32 i = 0; i < n; i += 2 * step)
    {
      fcplx t = cexpf(-I * PI * i / n) * out[i + step];
      buf[i / 2] = out[i] + t;
      buf[(i + n) / 2] = out[i] - t;
    }
  }
}

// https://rosettacode.org/wiki/Fast_Fourier_transform#C with slight modifications
void fft()
{
  fcplx out[BUFFER_SIZE]; // Could be dynamically allocated but with small buffer sizes it should work
  for (i32 i = 0; i < BUFFER_SIZE; ++i)
  {
    out[i] = audio.fft_buffer[i];
  }
  _fft(audio.fft_buffer, out, BUFFER_SIZE, 1);
}

// Calculates the magnitude of the fft, normalizes it and fills it into u_buffer
void fft_postprocess()
{
  f32 max_value = __FLT_MIN__;
  f32 min_value = __FLT_MAX__;
  for (u32 i = 0; i < BUFFER_SIZE; i++)
  {
    f32 magnitude = c2dB(audio.fft_buffer[i]);
    if (magnitude > max_value)
    {
      max_value = magnitude;
    }
    if (magnitude < min_value)
    {
      min_value = magnitude;
    }
  }
  f32 avg = 0;
  for (u32 i = 0; i < BUFFER_SIZE; ++i)
  {
    // Mapping the dB values between 0 and 1
    // Avoid division by zero
    f32 divisor = (max_value - min_value) != 0.0f ? (max_value - min_value) : 1.0f;
    f32 tmp = (c2dB(audio.fft_buffer[i]) - min_value) / divisor;
    // Avariging with the previous avg, it says in the same range, but it helps with the flickering between frames (tmp + audio.previous_avg) / 2.0
    unsigned char val = (unsigned char)Remap(tmp, 0.0, 1.0, 0.0, 255.0);
    audio.pixel_buffer[i] = (Color){.r = val, .g = audio.pixel_buffer[i].g, .b = (unsigned char)0, .a = (unsigned char)0};
    avg += tmp / BUFFER_SIZE;
  }
  audio.previous_avg = avg;
  UpdateTexture(shader_uniforms.u_buffer, audio.pixel_buffer);
}