/* Some known issues and limitations
 * - BUFFER_SIZE should be scaled with the music sample rate and the fps during runtime
 *   but shader arrays require a fixed length. We could solve this with using a bigger buffer size
 *   and padding the not used slots in the array with zeros, but then we need another uniform
 *   for telling the shader the current usable size of the array/buffer. This would cause I think
 *   unnecessary complexity with little improvement in quality.
 * - For now it only supports stereo audio files with 8/16/32 bit sample size.
 *   Also I might be wrong but raylib converts 24 bit audio files internally to 16 bit so it might
 *   be also supported. [Might be solved, now it supports every audio file that raylib can handle]
 * - I use a function called GetMusicFramesPlayed() in raylib.h but it can be replaced with:
 *   GetMusicTimePlayed(Music music) * music.stream.sampleRate;
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
#define SHIFT_FFT 1     // Shift the zero-frequency component to the center of the spectrum. If using FFT
#define USE_FFT 0       // Calculate the short time fft of the audio and send it to the shader, either use this or the USE_AMP, but not both.
#define USE_AMP 1       // Just calculate the "mono" amplitude of the audio and send it to the shader.
#define BUFFER_SIZE 512 // Should be equal in the shader also, which receives the buffer
#if (USE_FFT == 1) && (USE_AMP == 1)
#error "Both USE_FFT and USE_AMP cannot be defined as 1 at the same time."
#endif
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
  char *music_name;
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
  f32 *music_data; // Maybe not the most memory efficient method but raylib doesnt expose the music
                   // data and audio callbacks are a pain in the...
  u32 current_frame;
  bool audio_loaded;
  bool audio_flag;
#if USE_FFT
  fcplx fft_buffer[BUFFER_SIZE];
  f32 previous_avg; // Needed for smoothing out the fft
#endif
} Audio;

typedef struct shader_uniforms_struct
{
  f32 u_buffer[BUFFER_SIZE]; // Should be dynamically allocated if you use a bigger size. If the program crahes or sth. try allocating it on the heap
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
static void check_dropped_files();
#if USE_FFT
static void load_fft_buffer();
static void fft();
static void fft_postprocess();
#endif
#if USE_AMP
static void load_amp_buffer();
#endif

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

#if USE_FFT
  ui.shader = LoadShader(0, "shaders/base_fft.frag");
#endif
#if USE_AMP
  ui.shader = LoadShader(0, "shaders/base_amp.frag");
#endif

  /*
    uniform vec2 uResolution;
    uniform float uTime;
    uniform float uBuffer[BUFFER_SIZE];
  */
  shader_uniforms.u_buffer_loc = GetShaderLocation(ui.shader, "uBuffer");
  shader_uniforms.u_resolution_loc = GetShaderLocation(ui.shader, "uResolution");
  shader_uniforms.u_time_loc = GetShaderLocation(ui.shader, "uTime");

  // Initializing the ShaderUniorms struct
  shader_uniforms.u_time = 0.0f;
  memset(shader_uniforms.u_buffer, 0, sizeof(f32) * BUFFER_SIZE);
  shader_uniforms.u_resolution = (Vector2){.x = ui.canvas_bounds.width, .y = ui.canvas_bounds.height};

  // Initializing the audio struct and parsing if a filename was provided

  if (argc > 1)
  {
    char *file_path;
    file_path = argv[1];
    audio.audio_loaded = false;
    load_audio(file_path);
  }
  else
  {
    audio.audio_loaded = false;
    fprintf(stderr, "You didn't provide a file to load.\n"
                    "You can simply drop one onto the window or provide it as an argument.\n");
  }

#if DEBUG_MODE
  load_audio("songs/lens.mp3");
#endif

#if USE_FFT
  for (i32 i = 0; i < BUFFER_SIZE; ++i) // Maybe also memset() would work not sure
  {
    audio.fft_buffer[i] = 0.0f + 0.0f * I;
  }
  audio.previous_avg = 0.0f;
#endif

  // Main loop
  while (!WindowShouldClose())
  {

    check_dropped_files();

    if (IsKeyPressed(KEY_R))
    {
      UnloadShader(ui.shader);

#if USE_FFT
      ui.shader = LoadShader(0, "shaders/base_fft.frag");
#endif

#if USE_AMP
      ui.shader = LoadShader(0, "shaders/base_amp.frag");
#endif
      shader_uniforms.u_buffer_loc = GetShaderLocation(ui.shader, "uBuffer");
      shader_uniforms.u_resolution_loc = GetShaderLocation(ui.shader, "uResolution");
      shader_uniforms.u_time_loc = GetShaderLocation(ui.shader, "uTime");
    }

    if (IsKeyPressed(KEY_SPACE))
    {
      toggle_music_playing();
    }

    Vector2 m_pos = GetMousePosition();
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) // Sliding the music stream (Not using the sliders value because it messier that way)
    {
      if (CheckCollisionPointRec(m_pos, ui.progress_bounds))
      {
        f32 pos_in_secs = Remap(m_pos.x - ui.progress_bounds.x, 0.0f, ui.progress_bounds.width, 0.0f, GetMusicTimeLength(audio.music));
        SeekMusicStream(audio.music, pos_in_secs);
      }
    }

    if (IsWindowResized())
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

    if (audio.audio_loaded) // If we have a loaded music file
    {
      if (CheckCollisionPointRec(m_pos, ui.volume_hover_bounds) && !ui.volume_hovered)
      {
        ui.volume_hovered = true;
      }
      else if (!CheckCollisionPointRec(m_pos, ui.volume_slider_bounds) && ui.volume_hovered)
      {
        ui.volume_hovered = false;
      }

      UpdateMusicStream(audio.music);
      if (FloatEquals(0.0f, GetMusicTimePlayed(audio.music)) && audio.audio_flag)
      {

        if (!queue_is_empty(&ui.music_queue))
        {

          char *data = dequeue(&ui.music_queue);
          load_audio(data);
          free(data);
        }
        else
        {

          PlayMusicStream(audio.music);
        }
      }
      else if (FloatEquals(0.0f, GetMusicTimePlayed(audio.music)) && !audio.audio_flag)
      {

        audio.audio_flag = true;
      }

      audio.current_frame = GetMusicTimePlayed(audio.music) * audio.music.stream.sampleRate;

#if USE_FFT
      load_fft_buffer();
      fft();
      fft_postprocess();
#endif

#if USE_AMP
      load_amp_buffer();
#endif

      send_shader_uniforms();

      BeginDrawing();
      ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
      BeginShaderMode(ui.shader);
      DrawTexture(ui.canvas, ui.canvas_bounds.x, ui.canvas_bounds.y, BLANK); // We could flip the texture here but its much easier in the shader to just flip the coordinates
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
  CloseAudioDevice();
  CloseWindow();
  return 0;
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
  audio.audio_flag = false;
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
  SetShaderValueV(ui.shader, shader_uniforms.u_buffer_loc, shader_uniforms.u_buffer, SHADER_UNIFORM_FLOAT, BUFFER_SIZE);
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

    if (!IsMusicReady(audio.music))
    {
      char *data = dequeue(&ui.music_queue);
      load_audio(data);
      free(data);
    }
    UnloadDroppedFiles(fp);
    queue_print(&ui.music_queue);
  }
}

#if USE_FFT

void load_fft_buffer()
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

// Shift the zero-frequency component to the center of the spectrum.
void fftshift(f32 *data)
{
  f32 temp[BUFFER_SIZE];
  u32 shift = BUFFER_SIZE / 2;

  for (i32 i = 0; i < BUFFER_SIZE; i++)
  {
    temp[(i + shift) % BUFFER_SIZE] = data[i];
  }

  for (i32 i = 0; i < BUFFER_SIZE; i++)
  {
    data[i] = temp[i];
  }
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
    // Avariging with the previous avg, it says in the same range, but it helps with the flickering between frames
    shader_uniforms.u_buffer[i] = (tmp + audio.previous_avg) / 2.0;
    avg += tmp / BUFFER_SIZE;
  }
  audio.previous_avg = avg;
#if SHIFT_FFT
  fftshift(shader_uniforms.u_buffer);
#endif
}
#endif

#if USE_AMP
void load_amp_buffer()
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
    shader_uniforms.u_buffer[cnt] = audio.music_data[i];
    cnt++;
  }
}
#endif

/*
                 i                      n   i+BS
[0] [1] [2] [3] [4] [5] [6] [7] [8] [9]
                --- --- --- --- ---
*/