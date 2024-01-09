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

/*
typical samplerate = 44100 Hz f_nyquist = 22500 kHz because human hearing is around 20 Hz - 20 kHz
bin width of fft = samplig frequency / NFFT which is around 5.384 Hz
the positive frequencies are in the first half so 0..NFFT/2 - 1
For visualisation we dont really need the frequencies at really high frequencies
For example 1024*5.384 = 5513.216
            2048*5.384 = 11026.432 which is more than enough
*/

#define DEBUG_MODE 0
#define BUFFER_SIZE 2048
#define NFFT 8192
#define MAX_STRING_LEN 256
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
  char shader_filepath[MAX_STRING_LEN];
  Texture canvas;
  char music_name[MAX_STRING_LEN];
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
  Color pixel_buffer[BUFFER_SIZE];
  u32 current_frame;
  bool audio_loaded;
  i32 audio_flag; // We need this flag to know when an audio is over
  fcplx fft_out[NFFT];
  f32 fft_in[NFFT];
  f32 fft_in_windowed[NFFT];
  f32 fft_smooth[BUFFER_SIZE];
  f32 amp_buffer[BUFFER_SIZE];
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

void audio_callback(void *bufferData, u32 frames);
static void push_buffers(f32 value);
static void load_audio(const char *file_path);
static void send_shader_uniforms();
static void ui_draw();
static void toggle_music_playing();
static void resize_window();
static void check_dropped_files();
static void reload_shader(const char *file_path);
static void fft_prepare();
static void fft(f32 *in, fcplx *out, u32 stride, u32 n);
static void fft_postprocess();
// static f32 *load_wave_frames();
// static void load_audio_buffers();

int main(int argc, char **argv)
{
  // Initializing Raylib
  const u32 width = 75 * 16;
  const u32 height = 75 * 9;
#if (DEBUG_MODE == 0)
  SetTraceLogLevel(LOG_WARNING);
#endif
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
  InitWindow(width, height, "CShaderSound");
  SetWindowMinSize(16 * 60, 9 * 60);
  Image icon = LoadImage("icon.png");
  SetWindowIcon(icon);
  UnloadImage(icon);
  InitAudioDevice();
  SetMasterVolume(0.5f);
  SetTargetFPS(60);
  GuiLoadStyleDark();
  const i32 font_size = 28;
  Font font = LoadFontEx("anita_semi_square.ttf", font_size, NULL, 0);
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

  strcpy(ui.shader_filepath, "shaders/test.frag");
  ui.shader = LoadShader(0, ui.shader_filepath);

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
  Image temp = GenImageColor(BUFFER_SIZE, 1, BLANK); // .r/.x is the fft and .g/.y is the amplitude
  shader_uniforms.u_buffer = LoadTextureFromImage(temp);
  SetTextureFilter(shader_uniforms.u_buffer, TEXTURE_FILTER_BILINEAR); // Linear filtering
  SetTextureWrap(shader_uniforms.u_buffer, TEXTURE_WRAP_CLAMP);
  UnloadImage(temp);
  shader_uniforms.u_resolution = (Vector2){.x = ui.canvas_bounds.width, .y = ui.canvas_bounds.height};

  // Initializing the audio struct and parsing if a filename was provided
  memset(audio.pixel_buffer, 0, sizeof(audio.pixel_buffer));
  memset(audio.fft_in, 0, sizeof(audio.fft_in));
  memset(audio.fft_out, 0, sizeof(audio.fft_out));
  memset(audio.fft_smooth, 0, sizeof(audio.fft_smooth));
  memset(audio.amp_buffer, 0, sizeof(audio.amp_buffer));
#if DEBUG_MODE
  load_audio("songs/lens.mp3");
#endif

  // Main loop
  while (!WindowShouldClose())
  {

    check_dropped_files();

    if (IsKeyPressed(KEY_R))
    {
      reload_shader(ui.shader_filepath);
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
      audio.current_frame = (i32)(GetMusicTimePlayed(audio.music) * audio.music.stream.sampleRate);

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

      fft_prepare();
      fft(audio.fft_in_windowed, audio.fft_out, 1, NFFT);
      fft_postprocess();

      BeginDrawing();
      ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
      BeginShaderMode(ui.shader);
      send_shader_uniforms(); // We send them here outherwise the sampler2D would be reset
      // We flip the coordinates
      DrawTextureRec(ui.canvas, (Rectangle){.x = 0.f, .y = 0.f, .width = ui.canvas_bounds.width, .height = -ui.canvas_bounds.height}, (Vector2){.x = ui.canvas_bounds.x, .y = ui.canvas_bounds.y}, BLACK);
      EndShaderMode();
      ui_draw();
      EndDrawing();
    }
    else
    {
      i32 default_line_spacing = GuiGetStyle(DEFAULT, TEXT_LINE_SPACING);
      GuiSetStyle(DEFAULT, TEXT_LINE_SPACING, (i32)font_size * 1.5);
      BeginDrawing();
      ClearBackground(GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));
      GuiLabel((Rectangle){.x = 0.f, .y = 0.f, .width = GetScreenWidth(), .height = GetScreenHeight()}, "To start playing music,\n"
                                                                                                        "simply drop the file onto this window!\n"
                                                                                                        "You can also load shaders this way.\n"
                                                                                                        "(Only fragment shaders for now.)\n\n"
                                                                                                        "Press [ R ] for reloading the current shader.\n"
                                                                                                        "Pause/Resume music with [ SPACE ].\n");
      EndDrawing();
      GuiSetStyle(DEFAULT, TEXT_LINE_SPACING, default_line_spacing);
    }
  }

  if (IsMusicReady(audio.music))
  {
    DetachAudioStreamProcessor(audio.music.stream, audio_callback);
    UnloadMusicStream(audio.music);
  }
  UnloadFont(font);
  UnloadTexture(ui.canvas);
  UnloadTexture(shader_uniforms.u_buffer);
  UnloadShader(ui.shader);
  queue_destroy(&ui.music_queue);
  CloseAudioDevice();
  CloseWindow();
  return 0;
}

void reload_shader(const char *file_path)
{
  fprintf(stderr, "Trying to (re)load shader: %s\n", GetFileName(file_path));
  UnloadShader(ui.shader);
  strcpy(ui.shader_filepath, file_path);
  ui.shader = LoadShader(0, file_path);
  shader_uniforms.u_buffer_loc = GetShaderLocation(ui.shader, "uBuffer");
  shader_uniforms.u_resolution_loc = GetShaderLocation(ui.shader, "uResolution");
  shader_uniforms.u_time_loc = GetShaderLocation(ui.shader, "uTime");
  // { // Flashing the screen
  //   BeginDrawing();
  //   DrawRectangleLinesEx((Rectangle){0, 0, GetScreenWidth(), GetScreenHeight()}, 5.0f, GetColor(0x80FFDBFF));
  //   EndDrawing();
  //   WaitTime(0.1);
  // }
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
void load_audio(const char *file_path)
{
  audio.audio_loaded = false;
  strcpy(ui.music_name, GetFileNameWithoutExt(file_path));
  if (IsMusicReady(audio.music))
  {
    DetachAudioStreamProcessor(audio.music.stream, audio_callback);
    UnloadMusicStream(audio.music);
    memset(audio.pixel_buffer, 0, sizeof(audio.pixel_buffer));
    memset(audio.fft_in, 0, sizeof(audio.fft_in));
    memset(audio.fft_out, 0, sizeof(audio.fft_out));
    memset(audio.fft_smooth, 0, sizeof(audio.fft_smooth));
    memset(audio.amp_buffer, 0, sizeof(audio.amp_buffer));
  }
  audio.music = LoadMusicStream(file_path);
  audio.music.looping = false;
  // f32 time_waited = 0.0f;
  // f32 max_wait_time = 5.0f;
  // while (!IsMusicReady(audio.music)) // TODO Somekind of error checking if the music couldn't be loaded, but maybe raylib takes care of it
  // { // Wait for music to be initialized
  // if ((time_waited += GetFrameTime()) > max_wait_time)
  // {
  //   fprintf(stderr, "Maximum wait time [%f] for music [%s] loading exceeded!\nPlease try again!\n", max_wait_time, file_path);
  //   return;
  // }
  // }
  audio.audio_loaded = true;
  audio.audio_flag = 0;
  audio.current_frame = 0;
  AttachAudioStreamProcessor(audio.music.stream, audio_callback);
  PlayMusicStream(audio.music);
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
  static const char *supported_music_extensions[] = {".mp3", ".ogg", ".wav", ".qoa", ".xm", ".mod"};
  static const char *supported_fragment_shader_extensions[] = {".frag", ".fs", ".glsl", ".fsdr", ".fsh", ".fragment"};
  static const i32 se_cnt = 6;
  if (IsFileDropped())
  {
    FilePathList fp = LoadDroppedFiles();
    for (i32 i = 0; i < fp.count; i++)
    {
      const char *ext = GetFileExtension(fp.paths[i]);
      bool valid_extension = false;
      for (i32 j = 0; j < se_cnt; j++)
      {
        if (strcmp(ext, supported_music_extensions[j]) == 0)
        {
          enqueue(&ui.music_queue, fp.paths[i]);
          fprintf(stderr, "Queued: %s\n", GetFileName(fp.paths[i]));
          valid_extension = true;
          break;
        }
        else if (strcmp(ext, supported_fragment_shader_extensions[j]) == 0)
        {
          reload_shader(fp.paths[i]);
          valid_extension = true;
          break;
        }
      }
      if (!valid_extension)
      {
        fprintf(stderr, "Unsupported file extension [%s] for fragment shaders or audio files!\n", ext);
        fprintf(stderr, "Supported file extensions for fragment shaders:\n");
        for (i32 j = 0; j < se_cnt; j++)
        {
          fprintf(stderr, "%s\t", supported_fragment_shader_extensions[j]);
        }
        fprintf(stderr, "\nSupported file extensions for audio files:\n");
        for (i32 j = 0; j < se_cnt; j++)
        {
          fprintf(stderr, "%s\t", supported_music_extensions[j]);
        }
        fprintf(stderr, "\n");
      }
    }
    if (!IsMusicReady(audio.music) && !queue_is_empty(&ui.music_queue))
    {
      char *data = dequeue(&ui.music_queue);
      load_audio(data);
      free(data);
    }
    UnloadDroppedFiles(fp);
  }
}

#define FACTOR (2 * (NFFT / BUFFER_SIZE))

// Calculates the magnitude of the fft, normalizes it, and fills it into u_buffer
void fft_postprocess()
{

  f32 min_value = audio.fft_smooth[0];
  f32 max_value = audio.fft_smooth[0];

  f32 smoothing_factor = GetFrameTime() * (f32)FACTOR;
  for (u32 i = 0; i < BUFFER_SIZE; ++i) // Only interested in the lower frequency bins
  {
    f32 tmp = c2dB(audio.fft_out[i]);
    tmp = isinf(tmp) ? 0.0 : tmp;                                                                   // safety check
    audio.fft_smooth[i] = tmp * smoothing_factor + (1.0f - smoothing_factor) * audio.fft_smooth[i]; // (tmp - audio.fft_smooth[i]) *smoothing_factor;
    min_value = fminf(min_value, audio.fft_smooth[i]);
    max_value = fmaxf(max_value, audio.fft_smooth[i]);
  }

  // Update pixel buffer with the remapped values
  for (u32 i = 0; i < BUFFER_SIZE; ++i)
  {
    // Remap using the smoothed values
    unsigned char fft_val = (unsigned char)Remap(audio.fft_smooth[i], min_value, max_value, 0.0, 255.0);
    unsigned char amp_val = (unsigned char)Remap(audio.amp_buffer[i], -1.0f, 1.0f, 0.0f, 255.0f);
    // Update pixel buffer
    audio.pixel_buffer[i] = (Color){.r = fft_val, .g = amp_val, .b = (unsigned char)0, .a = (unsigned char)0};
  }
  // Update shader uniform with the smoothed values
  UpdateTexture(shader_uniforms.u_buffer, audio.pixel_buffer);
}

void audio_callback(void *bufferData, u32 frames)
{
  u32 ch = audio.music.stream.channels;
  f32 amp = 0;
  for (u32 i = 0; i < frames; i++)
  {
    switch (audio.music.stream.sampleSize)
    {
    case 8:
      amp = 0;
      for (i32 j = 0; j < ch; j++)
      {
        amp += ((f32)(((unsigned char *)bufferData)[ch * i + j] - 127) / 256.0f) / (f32)ch;
      }
      push_buffers(amp);
      break;
    case 16:
      amp = 0;
      for (i32 j = 0; j < ch; j++)
      {
        amp += ((f32)(((short *)bufferData)[ch * i + j]) / 32767.0f) / (f32)ch;
      }
      push_buffers(amp);
      break;
    case 32:
      amp = 0;
      for (i32 j = 0; j < ch; j++)
      {
        amp += (((f32 *)bufferData)[ch * i + j]) / (f32)ch;
      }
      push_buffers(amp);
      break;
    default:
      fprintf(stderr, "Sample size of music (%u) is not supported!\n", audio.music.stream.sampleSize);
      return;
    }
  }
}

void push_buffers(f32 value)
{
  memmove(audio.fft_in, audio.fft_in + 1, (NFFT - 1) * sizeof(audio.fft_in[0])); // Move everything to the left
  audio.fft_in[NFFT - 1] = value;                                                // Updating the most recent value
  memmove(audio.amp_buffer, audio.amp_buffer + 1, (BUFFER_SIZE - 1) * sizeof(audio.amp_buffer[0]));
  audio.amp_buffer[BUFFER_SIZE - 1] = value;
}

// from: https://github.com/tsoding/musializer and https://rosettacode.org/wiki/Fast_Fourier_transform
void fft(f32 *in, fcplx *out, u32 stride, u32 n)
{

  if (n == 1)
  {
    out[0] = in[0] + 0.0f * I;
    return;
  }

  fft(in, out, stride * 2, n / 2);
  fft(in + stride, out + n / 2, stride * 2, n / 2);

  for (u32 k = 0; k < n / 2; ++k)
  {
    f32 t = (f32)k / n;
    fcplx v = cexpf(-2 * I * PI * t) * out[k + n / 2];
    fcplx e = out[k];
    out[k] = e + v;
    out[k + n / 2] = e - v;
  }
}

void fft_prepare()
{
  for (i32 i = 0; i < NFFT; i++)
  {
    audio.fft_in_windowed[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / NFFT)) * audio.fft_in[i];
    // audio.fft_in_windowed[i] = (0.53836f*(1.0f - 0.53836f)*cosf(2*PI*i/NFFT)) * audio.fft_in[i];
    // audio.fft_in_windowed[i] = powf(sinf(PI*i/NFFT), 2.0f) * audio.fft_in[i];
  }
}