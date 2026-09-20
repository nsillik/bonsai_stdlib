#include <dlfcn.h>
#include <execinfo.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// NOTE(nsillik)(macos): The SDK's <MacTypes.h> declares `typedef SInt32 Fract;` and
// arrives transitively through every Foundation-based header -- Cocoa, AppKit, NSWindow.h,
// NSEvent.h -- and through Carbon.  That is a hard clash with maff.h's `f32 Fract(f32)`:
// C++ does not let a typedef and a function share a name, so importing AppKit at all fails
// the build with
//
//   error: redefinition of 'Fract' as different kind of symbol
//
// Renaming the SDK's typedef for the duration of the imports renames it everywhere those
// headers mention it, and MacTypes.h is include-guarded, so it is never re-processed under
// the original name: ours is the only Fract in scope from here on.
//
// The rename has to span *every* import that can pull in MacTypes.h first, not just Cocoa --
// whichever one gets there first emits the typedef, and it only gets one chance.  Renaming
// our Fract instead touches four stdlib headers and every caller, to fix a name that is
// correct on Linux and Windows.
#define Fract BonsaiSdkFract
#import <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>
#undef Fract

// NOTE(nsillik)(macos): Only for the NSOpenGL* surface that owns the context.
// <OpenGL/gl3.h> is deliberately not included: gl.h declares every GL constant this
// tree uses and loads every entry point through PlatformGetGlFunction.
#import <OpenGL/OpenGL.h>

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

  // NOTE(nsillik)(macos): A 4.1 core context genuinely lacks the GL 4.3+ entry
  // points, and where a missing one is fatal is decided by the `Initialized &= fn !=
  // 0` gates in gl.cpp -- in one place, for all platforms.  Error() here would trap
  // on the first absent symbol and hide the rest.
  if (!Result) { Warn("Couldn't load Opengl function (%s)", Name); }

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
  // NOTE(nsillik)(macos): A mach_port_t is a real u32, unlike pthread_t, which is a pointer
  // here, and a u32 is what a consumer of a thread id wants.
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
