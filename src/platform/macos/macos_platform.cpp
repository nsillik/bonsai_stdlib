#include <bonsai_stdlib/src/platform/linux/linux_file.cpp>

// NOTE(nsillik)(macos): Same shape as BindKeydownToInput/BindKeyupToInput in
// platform.h, but it selects the input field instead of writing it, so that one
// table serves both keydown and keyup rather than two 60-case switches that can
// drift apart.
#define BindMacKeyCode(KeyCode, InputField) case KeyCode: { Field = &Plat->Input.InputField; } break;

link_internal void
SetInputEvent(input_event *Event, b32 Down)
{
  if (Down)
  {
    Event->Clicked = True;
    Event->Pressed = True;
  }
  else
  {
    Event->Clicked  = False;
    Event->Pressed  = False;
    Event->Released = True;
  }

  return;
}

link_internal r32
BackingScaleFactor(os *Os)
{
  r32 Result = (r32)[Os->Window backingScaleFactor];
  return Result;
}

// NOTE(nsillik)(macos): ScreenDim is the size of the framebuffer, not the size of
// the window.  AppKit measures the window in points and the framebuffer in backing
// pixels, and the two differ by the backing scale factor (2 on Retina).  ScreenDim
// drives SetViewport and both projection matrices, so deriving it from the point
// size renders at half resolution on Retina, and looks correct while doing it.
// Hence the assertion: it pins the conversion, not the number.
link_internal void
UpdateScreenDimFromBacking(os *Os, platform *Plat)
{
  NSRect Bounds  = [Os->Display bounds];
  NSRect Backing = [Os->Display convertRectToBacking:Bounds];
  r32    Scale   = BackingScaleFactor(Os);

  Assert(Backing.size.width > 0 && Backing.size.height > 0);
  Assert(Abs(r32(Backing.size.width)  - r32(Bounds.size.width)  * Scale) <= 1.f);
  Assert(Abs(r32(Backing.size.height) - r32(Bounds.size.height) * Scale) <= 1.f);

  Plat->ScreenDim = V2(r32(Backing.size.width), r32(Backing.size.height));
  return;
}

link_internal void
UpdateMousePosition(os *Os, platform *Plat, NSEvent *Event)
{
  // locationInWindow is in window points; convertPoint:fromView:nil maps it into the
  // view's coordinate space, which is y-up.  X11 and win32 both report the origin at
  // the top-left, so the y term is flipped -- and everything is scaled into backing
  // pixels, because MouseP is compared against ScreenDim.
  NSPoint P     = [Os->Display convertPoint:[Event locationInWindow] fromView:nil];
  r32     Scale = BackingScaleFactor(Os);

  Plat->MouseP.x = (r32)P.x * Scale;
  Plat->MouseP.y = Plat->ScreenDim.y - (r32)P.y * Scale;
  return;
}

// NOTE(nsillik)(macos): The delegate is stateless and retained by never being released
// (this is MRC -- -x objective-c++ without -fobjc-arc); AppKit holds a window's delegate
// weakly, so anything it stored would have the os/platform lifetime to worry about.
@interface BonsaiWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation BonsaiWindowDelegate

- (void)windowWillClose:(NSNotification *)Notification
{
  GetStdlib()->Os.ContinueRunning = False;
}

// NOTE(nsillik)(macos): AppKit runs its own tracking loop on the main thread for the
// duration of a resize drag, so the game loop -- ProcessOsMessages included -- is not
// running while these fire.  They are still delivered, and still delivered on the main
// thread, which is the only thread permitted to touch the surface.  That is why the
// resize is handled here instead of by polling in the frame loop, and why the render
// thread keeps running through it: it never waits on the main thread, and the only thing
// they share, the CGL lock, is held for the length of a flush.
- (void)windowDidResize:(NSNotification *)Notification
{
  bonsai_stdlib *Stdlib = GetStdlib();
  if (!Stdlib->Os.GlContext) { return; }

  UpdateScreenDimFromBacking(&Stdlib->Os, &Stdlib->Plat);

  // -[NSOpenGLContext update] resizes the drawable to match the view.  Without it the
  // surface keeps whatever size it had when the context was attached, so a resized
  // window shows a scaled copy of the old size indefinitely.
  [Stdlib->Os.GlContext update];
}

// Moving the window between displays with different backing scale factors changes the
// framebuffer size without changing the window's size in points, so it does not come
// through windowDidResize.
- (void)windowDidChangeBackingProperties:(NSNotification *)Notification
{
  bonsai_stdlib *Stdlib = GetStdlib();
  if (!Stdlib->Os.GlContext) { return; }

  UpdateScreenDimFromBacking(&Stdlib->Os, &Stdlib->Plat);
  [Stdlib->Os.GlContext update];
}
@end

link_internal b32
OpenAndInitializeWindow(os *Os, platform *Plat, s32 VSyncFrames)
{
  // @duplicate_screen_dim_init_code
  v2i StartingWindowDim = V2i(1920, 1080);
  if (Plat->ScreenDim.x > 0.f && Plat->ScreenDim.y > 0.f) { StartingWindowDim = V2i(Plat->ScreenDim); }

  // A GUI app launched from a terminal is not activated by the window server by
  // default, so without this the window never takes focus and never gets a menu bar.
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

  BonsaiWindowDelegate *Delegate = [[BonsaiWindowDelegate alloc] init];
  [Window setDelegate:Delegate];
  [Window setReleasedWhenClosed:NO];
  [Window setTitle:@"Bonsai"];

  // Without this the window is never sent mouse-moved events, so MouseP only ever
  // updates while a button is held and nothing in the UI can be hovered.
  [Window setAcceptsMouseMovedEvents:YES];

  NSView *View = [Window contentView];

  // NOTE(nsillik)(macos): Must be set before the context is attached, because it is the
  // view that decides whether the surface is sized in points or in backing pixels.  Set
  // afterwards the framebuffer stays at half resolution on Retina.
  [View setWantsBestResolutionOpenGLSurface:YES];

  NSOpenGLContext *GlContext = [[NSOpenGLContext alloc] initWithFormat:PixelFormat shareContext:nil];
  if (!GlContext) { Error("Unable to create an NSOpenGLContext"); return False; }

  [GlContext setView:View];
  [GlContext makeCurrentContext];

  GLint SwapInterval = (VSyncFrames > 0) ? 1 : 0;
  [GlContext setValues:&SwapInterval forParameter:NSOpenGLCPSwapInterval];

  // Assigned before the window is ordered front, because the delegate is live from here
  // on and the resize callbacks it handles read Os->GlContext.
  Os->Window    = Window;
  Os->Display   = View;
  Os->GlContext = GlContext;

  [Window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  UpdateScreenDimFromBacking(Os, Plat);

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
      case NSEventTypeLeftMouseDown:  { SetInputEvent(&Plat->Input.LMB, True);  } break;
      case NSEventTypeLeftMouseUp:    { SetInputEvent(&Plat->Input.LMB, False); } break;
      case NSEventTypeRightMouseDown: { SetInputEvent(&Plat->Input.RMB, True);  } break;
      case NSEventTypeRightMouseUp:   { SetInputEvent(&Plat->Input.RMB, False); } break;

      // NOTE(nsillik)(macos): NSEventTypeOtherMouse* is every button past the first
      // two, and buttonNumber 2 is the middle one.  There is no input field for the
      // extra buttons, and neither win32 nor X11 bind them either.
      case NSEventTypeOtherMouseDown:
      {
        if ([Event buttonNumber] == 2) { SetInputEvent(&Plat->Input.MMB, True); }
      } break;

      case NSEventTypeOtherMouseUp:
      {
        if ([Event buttonNumber] == 2) { SetInputEvent(&Plat->Input.MMB, False); }
      } break;

      case NSEventTypeMouseMoved:
      case NSEventTypeLeftMouseDragged:
      case NSEventTypeRightMouseDragged:
      case NSEventTypeOtherMouseDragged:
      {
        UpdateMousePosition(Os, Plat, Event);
      } break;

      case NSEventTypeScrollWheel:
      {
        // X11 and win32 both report 120 units per wheel notch, and ui.cpp adds the
        // delta straight to its scroll offset, so a notch has to stay 120 here.  A
        // wheel reports one line per notch; a trackpad reports points, which are
        // already the right magnitude.  Positive is scroll-up, as on both other
        // platforms -- macOS has already applied the user's natural-scrolling
        // preference to this value.
        r32 RawDelta = (r32)[Event scrollingDeltaY];
        s32 Delta    = [Event hasPreciseScrollingDeltas] ? s32(RawDelta) : s32(RawDelta * 120.f);

        // Accumulated rather than assigned: a trackpad emits several scroll events per
        // frame and the whole queue is drained before anything reads the delta, so
        // assigning would drop all but the last.  ResetInputForFrameStart zeroes it, so
        // it accumulates for exactly one frame.
        Plat->Input.MouseWheelDelta += Delta;
      } break;

      case NSEventTypeFlagsChanged:
      {
        // NOTE(nsillik)(macos): Modifier keys arrive here and never through
        // keyDown/keyUp, and modifierFlags is the state of the whole keyboard, so all
        // three fields come from the one snapshot.
        NSEventModifierFlags Flags = [Event modifierFlags];

        SetInputEvent(&Plat->Input.Shift, (Flags & NSEventModifierFlagShift)   != 0);
        SetInputEvent(&Plat->Input.Ctrl,  (Flags & NSEventModifierFlagControl) != 0);
        SetInputEvent(&Plat->Input.Alt,   (Flags & NSEventModifierFlagOption)  != 0);
      } break;

      case NSEventTypeKeyDown:
      case NSEventTypeKeyUp:
      {
        // NOTE(nsillik)(macos): keyCode is a physical, layout-independent HIToolbox
        // virtual key code -- it does not move when the user switches to Dvorak or
        // AZERTY -- which is the same property the X11 keysym switches rely on.
        input_event *Field = 0;

        switch ([Event keyCode])
        {
          BindMacKeyCode(kVK_Return, Enter);
          BindMacKeyCode(kVK_Escape, Escape);

          // The key labelled "delete" above return is a backspace; forward-delete is a
          // separate key.  Matches VK_BACK/VK_DELETE and XK_BackSpace/XK_Delete.
          BindMacKeyCode(kVK_Delete,        Backspace);
          BindMacKeyCode(kVK_ForwardDelete, Delete);

          BindMacKeyCode(kVK_F1,  F1);
          BindMacKeyCode(kVK_F2,  F2);
          BindMacKeyCode(kVK_F3,  F3);
          BindMacKeyCode(kVK_F4,  F4);
          BindMacKeyCode(kVK_F5,  F5);
          BindMacKeyCode(kVK_F6,  F6);
          BindMacKeyCode(kVK_F7,  F7);
          BindMacKeyCode(kVK_F8,  F8);
          BindMacKeyCode(kVK_F9,  F9);
          BindMacKeyCode(kVK_F10, F10);
          BindMacKeyCode(kVK_F11, F11);
          BindMacKeyCode(kVK_F12, F12);

          BindMacKeyCode(kVK_ANSI_Period, Dot);
          BindMacKeyCode(kVK_ANSI_Minus,  Minus);
          BindMacKeyCode(kVK_ANSI_Slash,  FSlash);
          BindMacKeyCode(kVK_Space,       Space);

          BindMacKeyCode(kVK_ANSI_0, N0);
          BindMacKeyCode(kVK_ANSI_1, N1);
          BindMacKeyCode(kVK_ANSI_2, N2);
          BindMacKeyCode(kVK_ANSI_3, N3);
          BindMacKeyCode(kVK_ANSI_4, N4);
          BindMacKeyCode(kVK_ANSI_5, N5);
          BindMacKeyCode(kVK_ANSI_6, N6);
          BindMacKeyCode(kVK_ANSI_7, N7);
          BindMacKeyCode(kVK_ANSI_8, N8);
          BindMacKeyCode(kVK_ANSI_9, N9);

          BindMacKeyCode(kVK_ANSI_A, A);
          BindMacKeyCode(kVK_ANSI_B, B);
          BindMacKeyCode(kVK_ANSI_C, C);
          BindMacKeyCode(kVK_ANSI_D, D);
          BindMacKeyCode(kVK_ANSI_E, E);
          BindMacKeyCode(kVK_ANSI_F, F);
          BindMacKeyCode(kVK_ANSI_G, G);
          BindMacKeyCode(kVK_ANSI_H, H);
          BindMacKeyCode(kVK_ANSI_I, I);
          BindMacKeyCode(kVK_ANSI_J, J);
          BindMacKeyCode(kVK_ANSI_K, K);
          BindMacKeyCode(kVK_ANSI_L, L);
          BindMacKeyCode(kVK_ANSI_M, M);
          BindMacKeyCode(kVK_ANSI_N, N);
          BindMacKeyCode(kVK_ANSI_O, O);
          BindMacKeyCode(kVK_ANSI_P, P);
          BindMacKeyCode(kVK_ANSI_Q, Q);
          BindMacKeyCode(kVK_ANSI_R, R);
          BindMacKeyCode(kVK_ANSI_S, S);
          BindMacKeyCode(kVK_ANSI_T, T);
          BindMacKeyCode(kVK_ANSI_U, U);
          BindMacKeyCode(kVK_ANSI_V, V);
          BindMacKeyCode(kVK_ANSI_W, W);
          BindMacKeyCode(kVK_ANSI_X, X);
          BindMacKeyCode(kVK_ANSI_Y, Y);
          BindMacKeyCode(kVK_ANSI_Z, Z);

          default:
          {
          } break;
        }

        if (Field) { SetInputEvent(Field, [Event type] == NSEventTypeKeyDown); }
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

  // NOTE(nsillik)(macos): The lock is scoped to the flush rather than being held for
  // the render thread's whole lifetime.  -[NSOpenGLContext update] takes this same lock
  // internally and may only be called from the main thread, so a lock held across frames
  // hangs the first window resize.  Holding it here is all the mutual exclusion the
  // surface needs; the engine's RenderGate/FrameFence protocol already serializes the
  // render commands themselves.
  //
  // The context object goes through a local because -[NSOpenGLContext CGLContextObj]
  // is annotated NS_RETURNS_INNER_POINTER and so reads as nullable, while
  // CGLLockContext takes a _Nonnull CGLContextObj.
  CGLContextObj Ctx = [Os->GlContext CGLContextObj];
  CGLLockContext(Ctx);
  [Os->GlContext flushBuffer];
  CGLUnlockContext(Ctx);
}

link_internal void
PlatformMakeRenderContextCurrent(os *Os)
{
  [Os->GlContext makeCurrentContext];
}

link_internal void
PlatformReleaseRenderContext(os *Os)
{
  // NOTE(nsillik): [NSOpenGLContext clearCurrentContext] is a no-op unless the calling
  // thread is the one that made the context current, and the render thread is the only
  // thread that ever calls this.
  [NSOpenGLContext clearCurrentContext];
}

link_internal const char *
PlatformGetEnvironmentVar(const char *VarName, memory_arena *Memory)
{
  const char* Result = getenv(VarName);
  return Result;
}

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
