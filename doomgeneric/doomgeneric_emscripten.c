//doomgeneric emscripten port

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>

#include <stdbool.h>
#include <SDL.h>

#include <emscripten.h>
#include <emscripten/html5.h>

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
SDL_Texture* texture;

#define KEYQUEUE_SIZE 64
#define GAMEPAD_AXIS_PRESS_DEADZONE 0.35
#define GAMEPAD_AXIS_RELEASE_DEADZONE 0.25
#define GAMEPAD_TRIGGER_PRESS_DEADZONE 0.35
#define GAMEPAD_TRIGGER_RELEASE_DEADZONE 0.25
#define GAMEPAD_MAPPED_BUTTON_COUNT 17

#define GAMEPAD_BUTTON_A 0
#define GAMEPAD_BUTTON_B 1
#define GAMEPAD_BUTTON_X 2
#define GAMEPAD_BUTTON_Y 3
#define GAMEPAD_BUTTON_LEFT_SHOULDER 4
#define GAMEPAD_BUTTON_RIGHT_SHOULDER 5
#define GAMEPAD_BUTTON_LEFT_TRIGGER 6
#define GAMEPAD_BUTTON_RIGHT_TRIGGER 7
#define GAMEPAD_BUTTON_BACK 8
#define GAMEPAD_BUTTON_START 9
#define GAMEPAD_BUTTON_LEFT_STICK 10
#define GAMEPAD_BUTTON_RIGHT_STICK 11
#define GAMEPAD_BUTTON_DPAD_UP 12
#define GAMEPAD_BUTTON_DPAD_DOWN 13
#define GAMEPAD_BUTTON_DPAD_LEFT 14
#define GAMEPAD_BUTTON_DPAD_RIGHT 15
#define GAMEPAD_BUTTON_GUIDE 16

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;
static int s_ActiveGamepadIndex = -1;
static unsigned char s_GamepadKeyRefCount[256];
static unsigned char s_GamepadButtonState[GAMEPAD_MAPPED_BUTTON_COUNT];

typedef struct
{
  int axisIndex;
  unsigned char negativeKey;
  unsigned char positiveKey;
  int negativePressed;
  int positivePressed;
} axis_binding_t;

typedef struct
{
  int buttonIndex;
  unsigned char key;
  int pressed;
} trigger_binding_t;

static axis_binding_t s_AxisBindings[] = {
    {0, KEY_STRAFE_L, KEY_STRAFE_R, 0, 0},    // Left stick X
    {1, KEY_UPARROW, KEY_DOWNARROW, 0, 0},    // Left stick Y
    {2, KEY_LEFTARROW, KEY_RIGHTARROW, 0, 0}, // Right stick X
};

static trigger_binding_t s_TriggerBindings[] = {
    {GAMEPAD_BUTTON_LEFT_TRIGGER, KEY_LALT, 0},
    {GAMEPAD_BUTTON_RIGHT_TRIGGER, KEY_FIRE, 0},
};

static unsigned char convertToDoomKey(unsigned int key)
{
  switch (key)
    {
    case SDLK_RETURN:
      key = KEY_ENTER;
      break;
    case SDLK_ESCAPE:
      key = KEY_ESCAPE;
      break;
    case SDLK_LEFT:
      key = KEY_LEFTARROW;
      break;
    case SDLK_RIGHT:
      key = KEY_RIGHTARROW;
      break;
    case SDLK_UP:
      key = KEY_UPARROW;
      break;
    case SDLK_DOWN:
      key = KEY_DOWNARROW;
      break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
      key = KEY_FIRE;
      break;
    case SDLK_SPACE:
      key = KEY_USE;
      break;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
      key = KEY_RSHIFT;
      break;
    case SDLK_LALT:
    case SDLK_RALT:
      key = KEY_LALT;
      break;
    case SDLK_F2:
      key = KEY_F2;
      break;
    case SDLK_F3:
      key = KEY_F3;
      break;
    case SDLK_F4:
      key = KEY_F4;
      break;
    case SDLK_F5:
      key = KEY_F5;
      break;
    case SDLK_F6:
      key = KEY_F6;
      break;
    case SDLK_F7:
      key = KEY_F7;
      break;
    case SDLK_F8:
      key = KEY_F8;
      break;
    case SDLK_F9:
      key = KEY_F9;
      break;
    case SDLK_F10:
      key = KEY_F10;
      break;
    case SDLK_F11:
      key = KEY_F11;
      break;
    case SDLK_EQUALS:
    case SDLK_PLUS:
      key = KEY_EQUALS;
      break;
    case SDLK_MINUS:
      key = KEY_MINUS;
      break;
    default:
      key = tolower(key);
      break;
    }

  return key;
}

static void addDoomKeyToQueue(int pressed, unsigned char key)
{
  unsigned short keyData = (pressed << 8) | key;
  unsigned int nextIndex = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;

  // Drop oldest input on overflow to keep latest controls responsive.
  if (nextIndex == s_KeyQueueReadIndex)
  {
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;
  }

  s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
  s_KeyQueueWriteIndex = nextIndex;
}

static void addKeyToQueue(int pressed, unsigned int keyCode)
{
  unsigned char key = convertToDoomKey(keyCode);
  addDoomKeyToQueue(pressed, key);
}

static void gamepadKeyDown(unsigned char key)
{
  if (s_GamepadKeyRefCount[key] == 0)
  {
    addDoomKeyToQueue(1, key);
  }
  s_GamepadKeyRefCount[key]++;
}

static void gamepadKeyUp(unsigned char key)
{
  if (s_GamepadKeyRefCount[key] == 0)
  {
    return;
  }

  s_GamepadKeyRefCount[key]--;
  if (s_GamepadKeyRefCount[key] == 0)
  {
    addDoomKeyToQueue(0, key);
  }
}

static int isTriggerButtonIndex(int buttonIndex)
{
  return buttonIndex == GAMEPAD_BUTTON_LEFT_TRIGGER
      || buttonIndex == GAMEPAD_BUTTON_RIGHT_TRIGGER;
}

static unsigned char mapGamepadButtonToDoomKey(int buttonIndex)
{
  switch (buttonIndex)
  {
    case GAMEPAD_BUTTON_DPAD_UP:
      return KEY_UPARROW;
    case GAMEPAD_BUTTON_DPAD_DOWN:
      return KEY_DOWNARROW;
    case GAMEPAD_BUTTON_DPAD_LEFT:
      return KEY_LEFTARROW;
    case GAMEPAD_BUTTON_DPAD_RIGHT:
      return KEY_RIGHTARROW;
    case GAMEPAD_BUTTON_A:
      return KEY_USE;
    case GAMEPAD_BUTTON_B:
      return KEY_BACKSPACE;
    case GAMEPAD_BUTTON_X:
      return KEY_ENTER;
    case GAMEPAD_BUTTON_Y:
      return KEY_TAB;
    case GAMEPAD_BUTTON_LEFT_SHOULDER:
      return KEY_RSHIFT;
    case GAMEPAD_BUTTON_RIGHT_SHOULDER:
      return KEY_LALT;
    case GAMEPAD_BUTTON_LEFT_STICK:
      return KEY_RSHIFT;
    case GAMEPAD_BUTTON_RIGHT_STICK:
      return KEY_FIRE;
    case GAMEPAD_BUTTON_START:
      return KEY_ESCAPE;
    case GAMEPAD_BUTTON_BACK:
      return KEY_TAB;
    default:
      return 0;
  }
}

static void releaseAllGamepadKeys()
{
  unsigned int i;
  unsigned int j;

  for (i = 0; i < sizeof(s_AxisBindings) / sizeof(s_AxisBindings[0]); ++i)
  {
    axis_binding_t* binding = &s_AxisBindings[i];
    if (binding->negativePressed)
    {
      gamepadKeyUp(binding->negativeKey);
      binding->negativePressed = 0;
    }
    if (binding->positivePressed)
    {
      gamepadKeyUp(binding->positiveKey);
      binding->positivePressed = 0;
    }
  }

  for (i = 0; i < sizeof(s_TriggerBindings) / sizeof(s_TriggerBindings[0]); ++i)
  {
    trigger_binding_t* binding = &s_TriggerBindings[i];
    if (binding->pressed)
    {
      gamepadKeyUp(binding->key);
      binding->pressed = 0;
    }
  }

  for (i = 0; i < GAMEPAD_MAPPED_BUTTON_COUNT; ++i)
  {
    if (s_GamepadButtonState[i])
    {
      unsigned char doomKey = mapGamepadButtonToDoomKey((int)i);
      if (doomKey != 0)
      {
        gamepadKeyUp(doomKey);
      }
      s_GamepadButtonState[i] = 0;
    }
  }

  for (j = 0; j < sizeof(s_GamepadKeyRefCount); ++j)
  {
    while (s_GamepadKeyRefCount[j] > 0)
    {
      s_GamepadKeyRefCount[j]--;
      addDoomKeyToQueue(0, (unsigned char)j);
    }
  }
}

static void updateAxisBinding(int axisIndex, double value)
{
  unsigned int i;

  for (i = 0; i < sizeof(s_AxisBindings) / sizeof(s_AxisBindings[0]); ++i)
  {
    axis_binding_t* binding = &s_AxisBindings[i];
    int negativeNow = binding->negativePressed;
    int positiveNow = binding->positivePressed;

    if (binding->axisIndex != axisIndex)
    {
      continue;
    }

    if (binding->negativePressed)
    {
      negativeNow = (value < -GAMEPAD_AXIS_RELEASE_DEADZONE);
    }
    else
    {
      negativeNow = (value < -GAMEPAD_AXIS_PRESS_DEADZONE);
    }

    if (binding->positivePressed)
    {
      positiveNow = (value > GAMEPAD_AXIS_RELEASE_DEADZONE);
    }
    else
    {
      positiveNow = (value > GAMEPAD_AXIS_PRESS_DEADZONE);
    }

    if (negativeNow != binding->negativePressed)
    {
      if (negativeNow)
      {
        gamepadKeyDown(binding->negativeKey);
      }
      else
      {
        gamepadKeyUp(binding->negativeKey);
      }
      binding->negativePressed = negativeNow;
    }

    if (positiveNow != binding->positivePressed)
    {
      if (positiveNow)
      {
        gamepadKeyDown(binding->positiveKey);
      }
      else
      {
        gamepadKeyUp(binding->positiveKey);
      }
      binding->positivePressed = positiveNow;
    }
  }
}

static void updateTriggerBinding(const EmscriptenGamepadEvent* gamepad, int buttonIndex)
{
  unsigned int i;

  for (i = 0; i < sizeof(s_TriggerBindings) / sizeof(s_TriggerBindings[0]); ++i)
  {
    trigger_binding_t* binding = &s_TriggerBindings[i];
    int pressedNow = binding->pressed;
    int digitalPressed = 0;
    double analogValue = 0.0;

    if (binding->buttonIndex != buttonIndex)
    {
      continue;
    }

    if (buttonIndex < gamepad->numButtons)
    {
      digitalPressed = gamepad->digitalButton[buttonIndex];
      analogValue = gamepad->analogButton[buttonIndex];
    }

    if (binding->pressed)
    {
      pressedNow = digitalPressed || analogValue > GAMEPAD_TRIGGER_RELEASE_DEADZONE;
    }
    else
    {
      pressedNow = digitalPressed || analogValue > GAMEPAD_TRIGGER_PRESS_DEADZONE;
    }

    if (pressedNow != binding->pressed)
    {
      if (pressedNow)
      {
        gamepadKeyDown(binding->key);
      }
      else
      {
        gamepadKeyUp(binding->key);
      }
      binding->pressed = pressedNow;
    }
  }
}

static void updateDigitalButton(const EmscriptenGamepadEvent* gamepad, int buttonIndex)
{
  int downNow = 0;
  unsigned char doomKey;

  if (buttonIndex < gamepad->numButtons)
  {
    downNow = gamepad->digitalButton[buttonIndex];
  }

  if (s_GamepadButtonState[buttonIndex] == downNow)
  {
    return;
  }

  s_GamepadButtonState[buttonIndex] = downNow;
  doomKey = mapGamepadButtonToDoomKey(buttonIndex);

  if (doomKey == 0)
  {
    return;
  }

  if (downNow)
  {
    gamepadKeyDown(doomKey);
  }
  else
  {
    gamepadKeyUp(doomKey);
  }
}

static int getFirstConnectedGamepad(EmscriptenGamepadEvent* gamepad)
{
  int i;
  int count = emscripten_get_num_gamepads();

  for (i = 0; i < count; ++i)
  {
    if (emscripten_get_gamepad_status(i, gamepad) == EMSCRIPTEN_RESULT_SUCCESS
     && gamepad->connected)
    {
      return i;
    }
  }

  return -1;
}

static bool onGamepadConnected(int eventType, const EmscriptenGamepadEvent* gamepadEvent, void* userData)
{
  (void)eventType;
  (void)gamepadEvent;
  (void)userData;
  return true;
}

static bool onGamepadDisconnected(int eventType, const EmscriptenGamepadEvent* gamepadEvent, void* userData)
{
  (void)eventType;
  (void)userData;

  if (gamepadEvent != NULL && gamepadEvent->index == s_ActiveGamepadIndex)
  {
    releaseAllGamepadKeys();
    s_ActiveGamepadIndex = -1;
  }

  return true;
}

static void updateGamepadInput()
{
  EmscriptenGamepadEvent gamepad;
  int i;
  int activeIndex;

  if (emscripten_sample_gamepad_data() != EMSCRIPTEN_RESULT_SUCCESS)
  {
    return;
  }

  activeIndex = getFirstConnectedGamepad(&gamepad);
  if (activeIndex < 0)
  {
    if (s_ActiveGamepadIndex != -1)
    {
      releaseAllGamepadKeys();
      s_ActiveGamepadIndex = -1;
    }
    return;
  }

  if (s_ActiveGamepadIndex != activeIndex)
  {
    releaseAllGamepadKeys();
    s_ActiveGamepadIndex = activeIndex;
  }

  for (i = 0; i < (int)(sizeof(s_AxisBindings) / sizeof(s_AxisBindings[0])); ++i)
  {
    int axisIndex = s_AxisBindings[i].axisIndex;
    double axisValue = 0.0;

    if (axisIndex < gamepad.numAxes)
    {
      axisValue = gamepad.axis[axisIndex];
    }

    updateAxisBinding(axisIndex, axisValue);
  }

  for (i = 0; i < GAMEPAD_MAPPED_BUTTON_COUNT; ++i)
  {
    if (isTriggerButtonIndex(i))
    {
      updateTriggerBinding(&gamepad, i);
    }
    else
    {
      updateDigitalButton(&gamepad, i);
    }
  }
}

static void handleKeyInput()
{
  SDL_Event e;
  while (SDL_PollEvent(&e))
  {
    if (e.type == SDL_QUIT)
    {
      puts("Quit requested");
      atexit(SDL_Quit);
      exit(1);
    }

    if (e.type == SDL_KEYDOWN)
    {
      addKeyToQueue(1, e.key.keysym.sym);
    }
    else if (e.type == SDL_KEYUP)
    {
      addKeyToQueue(0, e.key.keysym.sym);
    }
  }
}


void DG_Init()
{
  emscripten_set_gamepadconnected_callback(NULL, false, onGamepadConnected);
  emscripten_set_gamepaddisconnected_callback(NULL, false, onGamepadDisconnected);

  window = SDL_CreateWindow("DOOM",
                            SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED,
                            DOOMGENERIC_RESX,
                            DOOMGENERIC_RESY,
                            SDL_WINDOW_SHOWN
                            );

  // Setup renderer
  renderer =  SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  // Clear winow
  SDL_RenderClear( renderer );
  // Render the rect to the screen
  SDL_RenderPresent(renderer);

  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

void DG_DrawFrame()
{
  SDL_UpdateTexture(texture, NULL, DG_ScreenBuffer, DOOMGENERIC_RESX*sizeof(uint32_t));

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);

  handleKeyInput();
  updateGamepadInput();
}

void DG_SleepMs(uint32_t ms)
{
  SDL_Delay(ms);
}

uint32_t DG_GetTicksMs()
{
  return SDL_GetTicks();
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
  if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
  {
    //key queue is empty
    return 0;
  }
  else
  {
    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex++;
    s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

    *pressed = keyData >> 8;
    *doomKey = keyData & 0xFF;

    return 1;
  }

  return 0;
}

void DG_SetWindowTitle(const char * title)
{
  if (window != NULL)
  {
    SDL_SetWindowTitle(window, title);
  }
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    emscripten_set_main_loop(doomgeneric_Tick, 0, 1);
    
    return 0;
}
