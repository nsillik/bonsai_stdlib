#include <dlfcn.h>
#include <execinfo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// NOTE(nsillik)(macos): Every Foundation-based SDK header -- Cocoa, AppKit,
// Foundation, NSWindow, NSEvent -- transitively drags in <MacTypes.h>, which
// declares `typedef SInt32 Fract;`.  That is a hard clash with
// bonsai_stdlib/src/maff.h's `f32 Fract(f32)`: C++ does not let a typedef and a
// function share a name, so including AppKit at all breaks the build with
// "redefinition of 'Fract' as different kind of symbol".
//
// Renaming the SDK's typedef for the duration of the include renames it
// consistently everywhere it is mentioned inside those headers (MacTypes.h is
// include-guarded, so it is never re-processed under the original name), and
// leaves our Fract as the only Fract in scope.
#define Fract BonsaiSdkFract
#import <Cocoa/Cocoa.h>
#undef Fract

#import <OpenGL/OpenGL.h>

// NOTE(nsillik)(macos): For the kVK_* virtual key codes.  There is no non-Carbon
// spelling of them, and they are what NSEvent's keyCode reports.
//
// This include MUST stay below the Cocoa import above: it transitively includes
// <MacTypes.h>, which is already include-guarded (and already renamed) by the time
// Cocoa has been through it.  Moved above the Cocoa import it reintroduces the real
// `typedef SInt32 Fract`, which breaks maff.h with the same 18 errors the rename
// exists to prevent.
#include <Carbon/Carbon.h>

// NOTE(nsillik)(macos): Deliberately <OpenGL/OpenGL.h> and not <OpenGL/gl3.h>.
// bonsai_stdlib/src/gl.h declares every GL constant and loads every entry point
// through PlatformGetGlFunction, so the only thing needed here is the
// NSOpenGL* surface that owns the context.

#define PLATFORM_RUNTIME_LIB_EXTENSION ".dylib"

typedef NSWindow        *window;
typedef NSView          *display;
typedef NSOpenGLContext *gl_context;

inline u64
GetCycleCount()
{
  u64 Result = __rdtsc();
  return Result;
}

link_internal b32
PlatformStdoutIsRedirected()
{
  b32 Result = isatty(fileno(stdout)) == 0;
  return Result;
}

void PlatformDebugStacktrace();

struct os
{
  window Window;
  display Display;
  gl_context GlContext;

  b32 ContinueRunning = True;
};

// @compat_with_windows_barf
inline s32
_chdir(const char* DirName)
{
  s32 Result = chdir(DirName);
  return Result;
}

link_internal void*
PlatformGetGlFunction(const char* Name)
{
  void *Result = dlsym(RTLD_DEFAULT, Name);
  if (!Result)
  {
    // NOTE(nsillik)(macos): A 4.1 core context genuinely lacks the 4.3+ entry
    // points, so a missing symbol is expected here rather than fatal -- and Error()
    // traps, which would abort the load on the first one and hide the rest.  Which
    // entry points are required is decided by the `Initialized &= fn != 0` gates in
    // bonsai_stdlib/src/gl.cpp, and reported in one place there.
    Warn("Couldn't load Opengl function (%s)", Name);
  }

  return Result;
}

link_internal b32
PlatformCreateDir(const char* Path, mode_t Mode = 0774)
{
  b32 Result = True;
  if (mkdir(Path, Mode) == -1)
  {
    Result = False;
  }
  return Result;
}

link_internal b32
PlatformDeleteDir(const char* Path, mode_t Mode = 0774)
{
  b32 Result = True;
  if (rmdir(Path) == -1)
  {
    Result = False;
  }
  return Result;
}

void *
OpenLibrary(const char *filename)
{
  void* Result = dlopen(filename, RTLD_NOW);

  if (!Result)
  {
    char *error = dlerror();
    Warn("OpenLibrary Failed (%s)", error);
  }
  else
  {
    Info("Library (%s) loaded!", filename);
  }

  return Result;
}

void
CloseLibrary(shared_lib Lib)
{
  s32 E = dlclose(Lib);
  if (E != 0)
  {
    Error("Closing Shared Library");
  }

  return;
}

inline void*
GetProcFromLib(shared_lib Lib, const char *Name)
{
  void* Result = dlsym(Lib, Name);
  if (Result == 0) { Warn("Unable to retrieve (%s) from shared library", Name); }
  return Result;
}

inline u32
GetCurrentThreadId()
{
  // NOTE(nsillik)(macos): pthread_t is a pointer here, so the (u32)pthread_self()
  // cast that works on Linux truncates the pointer.  mach_port_t is a real u32
  // and is what anything consuming a thread id actually wants.
  u32 Result = (u32)pthread_mach_thread_np(pthread_self());
  return Result;
}

link_internal b32
PlatformChangeDirectory(const char *Dir)
{
  b32 Result = (_chdir(Dir) == 0);
  return Result;
}

link_internal void
PlatformInitializeStdout(native_file *Stdout, native_file *Log);
