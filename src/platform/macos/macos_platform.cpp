#if BONSAI_NETWORK_IMPLEMENTATION
#include <bonsai_net/network.h>
#endif

#include <bonsai_stdlib/src/platform/linux/linux_file.cpp>

// NOTE(nsillik)(macos): Every file in the tree is compiled as Objective-C++ on
// macOS (see PLATFORM_CXX_OPTIONS in scripts/setup_for_cxx.sh), so AppKit can be
// used directly instead of through a separate .mm shim.

// TODO(nsillik)(macos): Deliberately lenient about the deprecations AppKit has
// accumulated; the alternatives are all macOS 14+ and the deployment target is
// 11.0.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface BonsaiWindowDelegate : NSObject <NSWindowDelegate>
{
@public
  b32 *ContinueRunning;
}
@end

@implementation BonsaiWindowDelegate
- (void)windowWillClose:(NSNotification *)Notification
{
  if (ContinueRunning) { *ContinueRunning = False; }
}
@end

link_internal b32
OpenAndInitializeWindow(os *Os, platform *Plat, s32 VSyncFrames)
{
  // @duplicate_screen_dim_init_code
  v2i StartingWindowDim = V2i(1920, 1080);
  if (Plat->ScreenDim.x > 0.f && Plat->ScreenDim.y > 0.f) { StartingWindowDim = V2i(Plat->ScreenDim); }

  // A GUI app launched from a terminal is not activated by the window server by
  // default, so without this the window never takes focus and never gets a menu
  // bar.
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [NSApp finishLaunching];

  NSOpenGLPixelFormatAttribute Attribs[] = {
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
    NSOpenGLPFAColorSize,     24,
    NSOpenGLPFAAlphaSize,     8,
    NSOpenGLPFADepthSize,     24,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAAccelerated,
    0
  };

  NSOpenGLPixelFormat *PixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:Attribs];
  if (!PixelFormat)
  {
    Error("Unable to create an NSOpenGLProfileVersion4_1Core pixel format");
    return False;
  }

  NSRect Frame = NSMakeRect(0, 0, StartingWindowDim.x, StartingWindowDim.y);
  NSWindowStyleMask Style = NSWindowStyleMaskTitled
                          | NSWindowStyleMaskClosable
                          | NSWindowStyleMaskMiniaturizable
                          | NSWindowStyleMaskResizable;

  NSWindow *Window = [[NSWindow alloc] initWithContentRect:Frame
                                                 styleMask:Style
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  if (!Window) { Error("Unable to create an NSWindow"); return False; }

  // NOTE(nsillik): The delegate is retained by the os struct's lifetime, not by
  // the window, because a closed window must not free it out from under us.
  BonsaiWindowDelegate *Delegate = [[BonsaiWindowDelegate alloc] init];
  Delegate->ContinueRunning = &Os->ContinueRunning;
  [Window setDelegate:Delegate];
  [Window setReleasedWhenClosed:NO];
  [Window setTitle:@"Bonsai"];

  NSView *View = [Window contentView];

  NSOpenGLContext *GlContext = [[NSOpenGLContext alloc] initWithFormat:PixelFormat shareContext:nil];
  if (!GlContext) { Error("Unable to create an NSOpenGLContext"); return False; }

  [GlContext setView:View];
  [GlContext makeCurrentContext];

  GLint SwapInterval = (VSyncFrames > 0) ? 1 : 0;
  [GlContext setValues:&SwapInterval forParameter:NSOpenGLCPSwapInterval];

  [Window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  Os->Window = Window;
  Os->Display = View;
  Os->GlContext = GlContext;

  // NOTE(nsillik)(macos): The framebuffer is in backing pixels while the window is
  // in points, and they differ by the backing scale factor (2 on Retina).
  // ScreenDim drives SetViewport, so it must be the backing size.
  [Os->Display setWantsBestResolutionOpenGLSurface:YES];
  NSRect BackingBounds = [Os->Display convertRectToBacking:[Os->Display bounds]];
  Plat->ScreenDim = V2(BackingBounds.size.width, BackingBounds.size.height);

  return True;
}

inline void
Terminate(os *Os, platform *Plat)
{
  if (Os->Window)
  {
    [Os->Window setDelegate:nil];
    [Os->Window close];
    Os->Window = 0;
  }

  Os->Display = 0;

  if (Os->GlContext)
  {
    [NSOpenGLContext clearCurrentContext];
    Os->GlContext = 0;
  }
}

inline r32
BackingScaleFactor(os *Os)
{
  r32 Result = (r32)[Os->Window backingScaleFactor];
  return Result;
}

b32
ProcessOsMessages(os *Os, platform *Plat)
{
  TIMED_FUNCTION();

  b32 EventFound = False;

  for (;;)
  {
    NSEvent *Event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantPast]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    if (!Event) { break; }

    EventFound = True;

    switch ([Event type])
    {
      case NSEventTypeLeftMouseDown:
      case NSEventTypeLeftMouseUp:
      case NSEventTypeRightMouseDown:
      case NSEventTypeRightMouseUp:
      case NSEventTypeOtherMouseDown:
      case NSEventTypeOtherMouseUp:
      case NSEventTypeLeftMouseDragged:
      case NSEventTypeRightMouseDragged:
      case NSEventTypeOtherMouseDragged:
      case NSEventTypeMouseMoved:
      {
        // locationInWindow is in window points; convertPoint:fromView:nil maps
        // it into the view's coordinate space, which is already y-up.
        NSPoint P = [Os->Display convertPoint:[Event locationInWindow] fromView:nil];
        r32 Scale = BackingScaleFactor(Os);
        NSRect BackingBounds = [Os->Display convertRectToBacking:[Os->Display bounds]];

        Plat->MouseP.x = (r32)P.x * Scale;
        Plat->MouseP.y = (r32)BackingBounds.size.height - (r32)P.y * Scale;

        // TODO(nsillik)(macos): Mouse buttons.  Needs the input table in
        // src/engine/input.h; see the NSEventTypeKeyDown TODO below.
      } break;

      case NSEventTypeScrollWheel:
      {
        // TODO(nsillik)(macos): scrollWheel -> Plat->MouseDP.
      } break;

      case NSEventTypeKeyDown:
      case NSEventTypeKeyUp:
      {
        // TODO(nsillik)(macos): keyCode is physical and layout independent, so it
        // maps to the input table in src/engine/input.h as a flat table, the
        // same shape as the two X11 keysym switches in linux_platform.cpp.  The
        // 63 input fields need an interactively-verified mapping, which is
        // Phase 2 work.
      } break;

      default:
      {
      } break;
    }

    [NSApp sendEvent:Event];
  }

  if (Os->Window && ![Os->Window isVisible]) { Os->ContinueRunning = False; }

  return EventFound;
}

inline void
BonsaiSwapBuffers(os *Os)
{
  TIMED_FUNCTION();

  CGLLockContext([Os->GlContext CGLContextObj]);
  [Os->GlContext flushBuffer];
  CGLUnlockContext([Os->GlContext CGLContextObj]);
}

link_internal void
PlatformMakeRenderContextCurrent(os *Os)
{
  CGLLockContext([Os->GlContext CGLContextObj]);
  [Os->GlContext makeCurrentContext];
}

link_internal void
PlatformReleaseRenderContext(os *Os)
{
  // NOTE(nsillik): [NSOpenGLContext clearCurrentContext] is a no-op unless the
  // calling thread is the one that made the context current, but the render
  // thread is the only thread that ever calls this.
  [NSOpenGLContext clearCurrentContext];
  CGLUnlockContext([Os->GlContext CGLContextObj]);
}

link_internal const char *
PlatformGetEnvironmentVar(const char *VarName, memory_arena *Memory)
{
  const char* Result = getenv(VarName);
  return Result;
}

#if BONSAI_NETWORK_IMPLEMENTATION
inline void
ConnectToServer(network_connection *Connection)
{
  if (!Connection->Socket.Id)
  {
    Connection->Socket = CreateSocket(Socket_NonBlocking);
  }

  errno = 0;
  s32 ConnectStatus = connect(Connection->Socket.Id,
                              (sockaddr *)&Connection->Address,
                              sizeof(sockaddr_in));

  if (ConnectStatus == 0)
  {
      DebugLine("Connected");
      Connection->State = ConnectionState_AwaitingHandshake;
  }
  else if (ConnectStatus == -1)
  {
    switch (errno)
    {
      case 0:
      {
      } break;

      case EINPROGRESS:
      case EALREADY:
      {
        // Connection in progress
      } break;

      case ECONNREFUSED:
      {
        // Host is down
      } break;

      case EISCONN:
      {
        // Not sure if we should ever call connect on an already-connected connection
        Assert(False);
      } break;

      default :
      {
        Error("Connecting to remote host encountered an unexpected error : %d", errno);
        Assert(False);
      } break;

    }
  }
  else
  {
    InvalidCodePath();
  }

  return;
}
#endif


void
PlatformDebugStacktrace()
{
  void *StackSymbols[32];
  s32 SymbolCount = backtrace(StackSymbols, 32);
  backtrace_symbols_fd(StackSymbols, SymbolCount, STDERR_FILENO);
  return;
}

link_internal void
PlatformInitializeStdout(native_file *StandardOutputFile, native_file *Log)
{
  StandardOutputFile->Handle = stdout;
  StandardOutputFile->Path = CSz("stdout");

  if (Log) { *Log = OpenFile("log.txt", FilePermission_Write); }
}


#if BONSAI_DEBUG_SYSTEM_API
link_internal void
Platform_EnableContextSwitchTracing()
{
  Warn("Context switch tracing not supported on macOS!");
}
#endif
