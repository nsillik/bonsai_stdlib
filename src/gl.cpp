
#define Draw(VertexCount) Draw_(VertexCount, __FUNCTION__)



link_internal debug_state *GetDebugState();

link_internal void
Draw_(u32 VertexCount, const char *Caller)
{
  TIMED_FUNCTION();
  Assert(VertexCount);

  // NOTE(Jesse): This is hella slow, so I'm just going to do a quick-n-dirty
  // version for the moment
  /* DEBUG_TRACK_DRAW_CALL(Caller, VertexCount); */

#if BONSAI_DEBUG_SYSTEM_API
  GetDebugState()->DrawCallCountLastFrame ++;
  GetDebugState()->VertexCountLastFrame += VertexCount;;
#endif

  AssertNoGlErrors;
  GetGL()->DrawArrays(GL_TRIANGLES, 0, s32(VertexCount) );
  AssertNoGlErrors;
}

inline void
SetDefaultFramebufferClearColors()
{
  GetGL()->ClearDepth(1.0); // NOTE(Jesse): Depth 1.0 is the furthest from the camera
  GetGL()->ClearColor(0.f, 0.f, 0.f, 1.f);
}

link_internal void
SetVSync(os *Os, s32 VSyncFrames)
{
  AssertNoGlErrors;

#if BONSAI_LINUX
  // TODO(Jesse, id: 151, tags: open_question, platform_linux): Not getting vsync on my arch laptop.
  PFNSWAPINTERVALPROC glSwapInterval = (PFNSWAPINTERVALPROC)PlatformGetGlFunction("glXSwapIntervalEXT");
  if ( glSwapInterval )
  { glSwapInterval(Os->Display, Os->Window, VSyncFrames); }
  else
  { Info("No Vsync"); }
#elif BONSAI_WIN32
  PFNSWAPINTERVALPROC glSwapInterval = (PFNSWAPINTERVALPROC)PlatformGetGlFunction("wglSwapIntervalEXT");
  if ( glSwapInterval )
  { glSwapInterval(VSyncFrames); }
  else
  { Info("No Vsync"); }
#elif EMCC
  // TODO(Jesse id: 368): How do we get vsync here?
  // @emcc_vsync
#endif

}

inline void
SetViewport(v2i Dim)
{
  GetGL()->Viewport(0, 0, Dim.x, Dim.y);
}

inline void
SetViewport(v2 Dim)
{
  GetGL()->Viewport(0, 0, (s32)Dim.x, (s32)Dim.y);
}

// NOTE(nsillik)(macos): The texture unit texture buffers are bound to.  Kept at the top of the
// range so it cannot collide with the units BindUniformById hands out, which start at 0 and
// increment per sampler -- the codebase already warns above 8, and a 4.1 core context reports 16
// (measured).  A samplerBuffer counts against the same limit as a sampler2D.
//
// The alternative is `layout(binding = N) uniform samplerBuffer`, which GLSL only accepts from
// 4.20 (ARB_shading_language_420pack, measured absent on macOS), so the unit has to be set through
// glUniform1i here.
#define SHADER_TEXTURE_BUFFER_UNIT 15

// NOTE(nsillik)(macos): Stands in for a std430 shader storage buffer on drivers that lack GL 4.3.
// The contents are viewed as GL_RGBA32F texels, so a texel is 16 bytes and the mapped type must be
// a multiple of that -- a struct whose members are padded to 16 bytes, which both users are.
//
// The sampler uniform is cached against the program id because glUniform1i affects the currently
// bound program and MultiDrawIndirect runs every frame.
struct texture_buffer_binding
{
  u32 Texture;
  u32 Buffer;
  u32 SamplerProgram;
  s32 SamplerUniform;
};

link_internal void
BindTextureBuffer(texture_buffer_binding *Binding, const char *SamplerName, void *Data, umm SizeBytes)
{
  auto GL = GetGL();

  Assert(SizeBytes % 16 == 0);

  if (Binding->Buffer == 0)
  {
    GL->GenBuffers(1, &Binding->Buffer);
    GL->GenTextures(1, &Binding->Texture);
    Assert(Binding->Buffer);
    Assert(Binding->Texture);
  }

  GL->BindBuffer(GL_TEXTURE_BUFFER, Binding->Buffer);
  GL->BufferData(GL_TEXTURE_BUFFER, Cast(GLsizeiptr, SizeBytes), Data, GL_DYNAMIC_DRAW);
  AssertNoGlErrors;

  GL->ActiveTexture(GL_TEXTURE0 + SHADER_TEXTURE_BUFFER_UNIT);
  GL->BindTexture(GL_TEXTURE_BUFFER, Binding->Texture);
  GL->TexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, Binding->Buffer);
  AssertNoGlErrors;

  GLuint Program = 0;
  GL->GetIntegerv(GL_CURRENT_PROGRAM, Cast(s32*, &Program));
  Assert(Program);

  if (Binding->SamplerProgram != Program)
  {
    Binding->SamplerProgram = Program;
    Binding->SamplerUniform = GL->GetUniformLocation(Program, SamplerName);

    // NOTE(nsillik): A miss means the shader does not declare the sampler -- the uniform would be
    // reading texel 0 of whatever is on unit 0 rather than failing, so this must not be silent.
    Assert(Binding->SamplerUniform >= 0);
  }

  GL->Uniform1i(Binding->SamplerUniform, SHADER_TEXTURE_BUFFER_UNIT);
  AssertNoGlErrors;
}

link_internal b32
InitializeOpenglFunctions()
{
  Info("Initializing OpenGL Extensions");

#if 0
  const char* glxExtensionString = glXQueryExtensionsString(Os->Display, DefaultScreen(Os->Display));
  const char* glExtensionString = (const char*)glGetString(GL_EXTENSIONS);
  Debug(glExtensionString);
  Debug(glxExtensionString);
#endif

  Assert(GetGL()->Initialized == False);
  GetGL()->Initialized = True;

  GetGL()->GetString                = (OpenglGetString)PlatformGetGlFunction("glGetString");
  GetGL()->Initialized              &= GetGL()->GetString != 0;

  s32 GLMajor = -1;
  s32 GLMinor = -1;

  if (GetGL()->GetString)
  {
    char* Vendor   = (char*)GetGL()->GetString(GL_VENDOR);
    char* Renderer = (char*)GetGL()->GetString(GL_RENDERER);
    char* Version  = (char*)GetGL()->GetString(GL_VERSION);
    if (Vendor && Renderer && Version)
    {
      Info("Opengl Driver Info : (%s) (%s) (%s)", Vendor, Renderer, Version);

      GetGL()->GetError                  = (OpenglGetError)PlatformGetGlFunction("glGetError");
      GetGL()->Initialized               &= GetGL()->GetError != 0;

      GetGL()->Enable                    = (OpenglEnable)PlatformGetGlFunction("glEnable");
      GetGL()->Initialized               &= GetGL()->Enable != 0;

      GetGL()->Disable                   = (OpenglDisable)PlatformGetGlFunction("glDisable");
      GetGL()->Initialized               &= GetGL()->Disable != 0;

      GetGL()->CullFace                  = (OpenglCullFace)PlatformGetGlFunction("glCullFace");
      GetGL()->Initialized               &= GetGL()->CullFace != 0;

      GetGL()->Viewport                  = (OpenglViewport)PlatformGetGlFunction("glViewport");
      GetGL()->Initialized               &= GetGL()->Viewport != 0;

      GetGL()->DepthFunc                 = (OpenglDepthFunc)PlatformGetGlFunction("glDepthFunc");
      GetGL()->Initialized               &= GetGL()->DepthFunc != 0;

      GetGL()->BlendFunc                 = (OpenglBlendFunc)PlatformGetGlFunction("glBlendFunc");
      GetGL()->Initialized               &= GetGL()->BlendFunc != 0;

      GetGL()->DrawArrays                = (OpenglDrawArrays)PlatformGetGlFunction("glDrawArrays");
      GetGL()->Initialized               &= GetGL()->DrawArrays != 0;

      GetGL()->DrawArraysIndirect        = (OpenglDrawArraysIndirect)PlatformGetGlFunction("glDrawArraysIndirect");
      GetGL()->Initialized               &= GetGL()->DrawArraysIndirect != 0;

      // NOTE(nsillik)(macos): Loaded but deliberately not required.  glMultiDrawArraysIndirect is
      // GL 4.3 and is absent on macOS' 4.1 core context, and nothing calls it: MultiDrawIndirect
      // supplies the draw index to the shader itself and issues one direct draw per command.
      GetGL()->MultiDrawArraysIndirect   = (OpenglMultiDrawArraysIndirect)PlatformGetGlFunction("glMultiDrawArraysIndirect");

      GetGL()->Clear                     = (OpenglClear)PlatformGetGlFunction("glClear");
      GetGL()->Initialized               &= GetGL()->Clear != 0;

      GetGL()->ClearColor                = (OpenglClearColor)PlatformGetGlFunction("glClearColor");
      GetGL()->Initialized               &= GetGL()->ClearColor != 0;

      GetGL()->ClearDepth                = (OpenglClearDepth)PlatformGetGlFunction("glClearDepth");
      GetGL()->Initialized               &= GetGL()->ClearDepth != 0;

      GetGL()->GenTextures               = (OpenglGenTextures)PlatformGetGlFunction("glGenTextures");
      GetGL()->Initialized               &= GetGL()->GenTextures != 0;


      GetGL()->BindTexture               = (OpenglBindTexture)PlatformGetGlFunction("glBindTexture");
      GetGL()->Initialized               &= GetGL()->BindTexture != 0;

      GetGL()->DeleteTextures            = (OpenglDeleteTextures)PlatformGetGlFunction("glDeleteTextures");
      GetGL()->Initialized               &= GetGL()->DeleteTextures != 0;

      GetGL()->ActiveTexture             = (OpenglActiveTexture)PlatformGetGlFunction("glActiveTexture");
      GetGL()->Initialized               &= GetGL()->ActiveTexture != 0;

      GetGL()->GetTexImage               = (OpenglGetTexImage)PlatformGetGlFunction("glGetTexImage");
      GetGL()->Initialized               &= GetGL()->GetTexImage != 0;

      GetGL()->ReadPixels                = (OpenglReadPixels)PlatformGetGlFunction("glReadPixels");
      GetGL()->Initialized               &= GetGL()->ReadPixels != 0;

      GetGL()->TexStorage1D              = (OpenglTexStorage1D)PlatformGetGlFunction("glTexStorage1D");
      GetGL()->Initialized               &= GetGL()->TexStorage1D != 0;

      GetGL()->TexStorage2D              = (OpenglTexStorage2D)PlatformGetGlFunction("glTexStorage2D");
      GetGL()->Initialized               &= GetGL()->TexStorage2D != 0;

      GetGL()->TexStorage3D              = (OpenglTexStorage3D)PlatformGetGlFunction("glTexStorage3D");
      GetGL()->Initialized               &= GetGL()->TexStorage3D != 0;

      GetGL()->TexImage1D                = (OpenglTexImage1D)PlatformGetGlFunction("glTexImage1D");
      GetGL()->Initialized               &= GetGL()->TexImage1D != 0;

      GetGL()->TexImage2D                = (OpenglTexImage2D)PlatformGetGlFunction("glTexImage2D");
      GetGL()->Initialized               &= GetGL()->TexImage2D != 0;

      GetGL()->TexImage3D                = (OpenglTexImage3D)PlatformGetGlFunction("glTexImage3D");
      GetGL()->Initialized               &= GetGL()->TexImage3D != 0;

      GetGL()->TexSubImage1D             = (OpenglTexSubImage1D)PlatformGetGlFunction("glTexSubImage1D");
      GetGL()->Initialized               &= GetGL()->TexSubImage1D != 0;

      GetGL()->TexSubImage2D             = (OpenglTexSubImage2D)PlatformGetGlFunction("glTexSubImage2D");
      GetGL()->Initialized               &= GetGL()->TexSubImage2D != 0;

      GetGL()->TexSubImage3D             = (OpenglTexSubImage3D)PlatformGetGlFunction("glTexSubImage3D");
      GetGL()->Initialized               &= GetGL()->TexSubImage3D != 0;

      GetGL()->TexParameterf             = (OpenglTexParameterf)PlatformGetGlFunction("glTexParameterf");
      GetGL()->Initialized               &= GetGL()->TexParameterf != 0;

      GetGL()->TexParameterfv            = (OpenglTexParameterfv)PlatformGetGlFunction("glTexParameterfv");
      GetGL()->Initialized               &= GetGL()->TexParameterfv != 0;

      GetGL()->TexParameteri             = (OpenglTexParameteri)PlatformGetGlFunction("glTexParameteri");
      GetGL()->Initialized               &= GetGL()->TexParameteri != 0;

      GetGL()->TexParameteriv            = (OpenglTexParameteriv)PlatformGetGlFunction("glTexParameteriv");
      GetGL()->Initialized               &= GetGL()->TexParameteriv != 0;

      GetGL()->TexBuffer                 = (OpenglTexBuffer)PlatformGetGlFunction("glTexBuffer");
      GetGL()->Initialized               &= GetGL()->TexBuffer != 0;

      GetGL()->CompressedTexImage3D      = (OpenglCompressedTexImage3D)PlatformGetGlFunction("glCompressedTexImage3D");
      GetGL()->Initialized               &= GetGL()->CompressedTexImage3D != 0;

      GetGL()->CompressedTexImage2D      = (OpenglCompressedTexImage2D)PlatformGetGlFunction("glCompressedTexImage2D");
      GetGL()->Initialized               &= GetGL()->CompressedTexImage2D != 0;

      GetGL()->CompressedTexImage1D      = (OpenglCompressedTexImage1D)PlatformGetGlFunction("glCompressedTexImage1D");
      GetGL()->Initialized               &= GetGL()->CompressedTexImage1D != 0;

      GetGL()->PixelStoref               = (OpenglPixelStoref)PlatformGetGlFunction("glPixelStoref");
      GetGL()->Initialized               &= GetGL()->PixelStoref != 0;

      GetGL()->PixelStorei               = (OpenglPixelStorei)PlatformGetGlFunction("glPixelStorei");
      GetGL()->Initialized               &= GetGL()->PixelStorei != 0;

      GetGL()->EnableVertexAttribArray   = (OpenglEnableVertexAttribArray)PlatformGetGlFunction("glEnableVertexAttribArray");
      GetGL()->Initialized               &= GetGL()->EnableVertexAttribArray != 0;

      GetGL()->DisableVertexAttribArray  = (OpenglDisableVertexAttribArray)PlatformGetGlFunction("glDisableVertexAttribArray");
      GetGL()->Initialized               &= GetGL()->DisableVertexAttribArray != 0;

      GetGL()->VertexAttribPointer       = (OpenglVertexAttribPointer)PlatformGetGlFunction("glVertexAttribPointer");
      GetGL()->Initialized               &= GetGL()->VertexAttribPointer != 0;

      GetGL()->VertexAttribIPointer       = (OpenglVertexAttribIPointer)PlatformGetGlFunction("glVertexAttribIPointer");
      GetGL()->Initialized               &= GetGL()->VertexAttribIPointer != 0;

      GetGL()->BindFramebuffer           = (OpenglBindFramebuffer)PlatformGetGlFunction("glBindFramebuffer");
      GetGL()->Initialized               &= GetGL()->BindFramebuffer != 0;

      GetGL()->DeleteFramebuffers        = (OpenglDeleteFramebuffers)PlatformGetGlFunction("glDeleteFramebuffers");
      GetGL()->Initialized               &= GetGL()->DeleteFramebuffers != 0;

      GetGL()->GenFramebuffers           = (OpenglGenFramebuffers)PlatformGetGlFunction("glGenFramebuffers");
      GetGL()->Initialized               &= GetGL()->GenFramebuffers != 0;

      GetGL()->CheckFramebufferStatus    = (OpenglCheckFramebufferStatus)PlatformGetGlFunction("glCheckFramebufferStatus");
      GetGL()->Initialized               &= GetGL()->CheckFramebufferStatus != 0;

      GetGL()->FramebufferTexture1D      = (OpenglFramebufferTexture1D)PlatformGetGlFunction("glFramebufferTexture1D");
      GetGL()->Initialized               &= GetGL()->FramebufferTexture1D != 0;

      GetGL()->FramebufferTexture2D      = (OpenglFramebufferTexture2D)PlatformGetGlFunction("glFramebufferTexture2D");
      GetGL()->Initialized               &= GetGL()->FramebufferTexture2D != 0;

      GetGL()->FramebufferTexture3D      = (OpenglFramebufferTexture3D)PlatformGetGlFunction("glFramebufferTexture3D");
      GetGL()->Initialized               &= GetGL()->FramebufferTexture3D != 0;

      GetGL()->FramebufferTextureLayer   = (OpenglFramebufferTextureLayer)PlatformGetGlFunction("glFramebufferTextureLayer");
      GetGL()->Initialized               &= GetGL()->FramebufferTextureLayer != 0;

      GetGL()->FramebufferRenderbuffer   = (OpenglFramebufferRenderbuffer)PlatformGetGlFunction("glFramebufferRenderbuffer");
      GetGL()->Initialized               &= GetGL()->FramebufferRenderbuffer != 0;

      GetGL()->CompileShader             = (OpenglCompileShader)PlatformGetGlFunction("glCompileShader");
      GetGL()->Initialized               &= GetGL()->CompileShader != 0;

      GetGL()->CreateProgram             = (OpenglCreateProgram)PlatformGetGlFunction("glCreateProgram");
      GetGL()->Initialized               &= GetGL()->CreateProgram != 0;

      GetGL()->CreateShader              = (OpenglCreateShader)PlatformGetGlFunction("glCreateShader");
      GetGL()->Initialized               &= GetGL()->CreateShader != 0;

      GetGL()->LinkProgram               = (OpenglLinkProgram)PlatformGetGlFunction("glLinkProgram");
      GetGL()->Initialized               &= GetGL()->LinkProgram != 0;

      GetGL()->ShaderSource              = (OpenglShaderSource)PlatformGetGlFunction("glShaderSource");
      GetGL()->Initialized               &= GetGL()->ShaderSource != 0;

      GetGL()->GetShaderSource           = (OpenglGetShaderSource)PlatformGetGlFunction("glGetShaderSource");
      GetGL()->Initialized               &= GetGL()->GetShaderSource != 0;

      GetGL()->UseProgram                = (OpenglUseProgram)PlatformGetGlFunction("glUseProgram");
      GetGL()->Initialized               &= GetGL()->UseProgram != 0;

      GetGL()->GetProgramInfoLog         = (OpenglGetProgramInfoLog)PlatformGetGlFunction("glGetProgramInfoLog");
      GetGL()->Initialized               &= GetGL()->GetProgramInfoLog != 0;

      GetGL()->GetShaderInfoLog          = (OpenglGetShaderInfoLog)PlatformGetGlFunction("glGetShaderInfoLog");
      GetGL()->Initialized               &= GetGL()->GetShaderInfoLog != 0;

      GetGL()->DeleteProgram             = (OpenglDeleteProgram)PlatformGetGlFunction("glDeleteProgram");
      GetGL()->Initialized               &= GetGL()->DeleteProgram != 0;

      GetGL()->DeleteShader              = (OpenglDeleteShader)PlatformGetGlFunction("glDeleteShader");
      GetGL()->Initialized               &= GetGL()->DeleteShader != 0;

      GetGL()->DetachShader              = (OpenglDetachShader)PlatformGetGlFunction("glDetachShader");
      GetGL()->Initialized               &= GetGL()->DetachShader != 0;

      GetGL()->Uniform1f                 = (OpenglUniform1f)PlatformGetGlFunction("glUniform1f");
      GetGL()->Initialized               &= GetGL()->Uniform1f != 0;

      GetGL()->Uniform2f                 = (OpenglUniform2f)PlatformGetGlFunction("glUniform2f");
      GetGL()->Initialized               &= GetGL()->Uniform2f != 0;

      GetGL()->Uniform3f                 = (OpenglUniform3f)PlatformGetGlFunction("glUniform3f");
      GetGL()->Initialized               &= GetGL()->Uniform3f != 0;

      GetGL()->Uniform4f                 = (OpenglUniform4f)PlatformGetGlFunction("glUniform4f");
      GetGL()->Initialized               &= GetGL()->Uniform4f != 0;

      GetGL()->Uniform1i                 = (OpenglUniform1i)PlatformGetGlFunction("glUniform1i");
      GetGL()->Initialized               &= GetGL()->Uniform1i != 0;

      GetGL()->Uniform2i                 = (OpenglUniform2i)PlatformGetGlFunction("glUniform2i");
      GetGL()->Initialized               &= GetGL()->Uniform2i != 0;

      GetGL()->Uniform3i                 = (OpenglUniform3i)PlatformGetGlFunction("glUniform3i");
      GetGL()->Initialized               &= GetGL()->Uniform3i != 0;

      GetGL()->Uniform4i                 = (OpenglUniform4i)PlatformGetGlFunction("glUniform4i");
      GetGL()->Initialized               &= GetGL()->Uniform4i != 0;

      GetGL()->Uniform1fv                = (OpenglUniform1fv)PlatformGetGlFunction("glUniform1fv");
      GetGL()->Initialized               &= GetGL()->Uniform1fv != 0;

      GetGL()->Uniform2fv                = (OpenglUniform2fv)PlatformGetGlFunction("glUniform2fv");
      GetGL()->Initialized               &= GetGL()->Uniform2fv != 0;

      GetGL()->Uniform3fv                = (OpenglUniform3fv)PlatformGetGlFunction("glUniform3fv");
      GetGL()->Initialized               &= GetGL()->Uniform3fv != 0;

      GetGL()->Uniform4fv                = (OpenglUniform4fv)PlatformGetGlFunction("glUniform4fv");
      GetGL()->Initialized               &= GetGL()->Uniform4fv != 0;

      GetGL()->Uniform1iv                = (OpenglUniform1iv)PlatformGetGlFunction("glUniform1iv");
      GetGL()->Initialized               &= GetGL()->Uniform1iv != 0;

      GetGL()->Uniform2iv                = (OpenglUniform2iv)PlatformGetGlFunction("glUniform2iv");
      GetGL()->Initialized               &= GetGL()->Uniform2iv != 0;

      GetGL()->Uniform3iv                = (OpenglUniform3iv)PlatformGetGlFunction("glUniform3iv");
      GetGL()->Initialized               &= GetGL()->Uniform3iv != 0;

      GetGL()->Uniform4iv                = (OpenglUniform4iv)PlatformGetGlFunction("glUniform4iv");
      GetGL()->Initialized               &= GetGL()->Uniform4iv != 0;

      GetGL()->UniformMatrix2fv          = (OpenglUniformMatrix2fv)PlatformGetGlFunction("glUniformMatrix2fv");
      GetGL()->Initialized               &= GetGL()->UniformMatrix2fv != 0;

      GetGL()->UniformMatrix3fv          = (OpenglUniformMatrix3fv)PlatformGetGlFunction("glUniformMatrix3fv");
      GetGL()->Initialized               &= GetGL()->UniformMatrix3fv != 0;

      GetGL()->UniformMatrix4fv          = (OpenglUniformMatrix4fv)PlatformGetGlFunction("glUniformMatrix4fv");
      GetGL()->Initialized               &= GetGL()->UniformMatrix4fv != 0;

      GetGL()->Uniform1ui                = (OpenglUniform1ui)PlatformGetGlFunction("glUniform1ui");
      GetGL()->Initialized               &= GetGL()->Uniform1ui != 0;

      GetGL()->Uniform2ui                = (OpenglUniform2ui)PlatformGetGlFunction("glUniform2ui");
      GetGL()->Initialized               &= GetGL()->Uniform2ui != 0;

      GetGL()->Uniform3ui                = (OpenglUniform3ui)PlatformGetGlFunction("glUniform3ui");
      GetGL()->Initialized               &= GetGL()->Uniform3ui != 0;

      GetGL()->Uniform4ui                = (OpenglUniform4ui)PlatformGetGlFunction("glUniform4ui");
      GetGL()->Initialized               &= GetGL()->Uniform4ui != 0;

      GetGL()->Uniform1uiv               = (OpenglUniform1uiv)PlatformGetGlFunction("glUniform1uiv");
      GetGL()->Initialized               &= GetGL()->Uniform1uiv != 0;

      GetGL()->Uniform2uiv               = (OpenglUniform2uiv)PlatformGetGlFunction("glUniform2uiv");
      GetGL()->Initialized               &= GetGL()->Uniform2uiv != 0;

      GetGL()->Uniform3uiv               = (OpenglUniform3uiv)PlatformGetGlFunction("glUniform3uiv");
      GetGL()->Initialized               &= GetGL()->Uniform3uiv != 0;

      GetGL()->Uniform4uiv               = (OpenglUniform4uiv)PlatformGetGlFunction("glUniform4uiv");
      GetGL()->Initialized               &= GetGL()->Uniform4uiv != 0;

      GetGL()->GetUniformLocation        = (OpenglGetUniformLocation)PlatformGetGlFunction("glGetUniformLocation");
      GetGL()->Initialized               &= GetGL()->GetUniformLocation != 0;

      GetGL()->GetShaderiv               = (OpenglGetShaderiv)PlatformGetGlFunction("glGetShaderiv");
      GetGL()->Initialized               &= GetGL()->GetShaderiv != 0;

      GetGL()->GetProgramiv              = (OpenglGetProgramiv)PlatformGetGlFunction("glGetProgramiv");
      GetGL()->Initialized               &= GetGL()->GetProgramiv != 0;

      GetGL()->AttachShader              = (OpenglAttachShader)PlatformGetGlFunction("glAttachShader");
      GetGL()->Initialized               &= GetGL()->AttachShader != 0;

      GetGL()->BindBuffer                = (OpenglBindBuffer)PlatformGetGlFunction("glBindBuffer");
      GetGL()->Initialized               &= GetGL()->BindBuffer != 0;

      GetGL()->BindBufferBase            = (OpenglBindBufferBase)PlatformGetGlFunction("glBindBufferBase");
      GetGL()->Initialized               &= GetGL()->BindBufferBase != 0;

      GetGL()->BindVertexArray           = (OpenglBindVertexArray)PlatformGetGlFunction("glBindVertexArray");
      GetGL()->Initialized               &= GetGL()->BindVertexArray != 0;

      GetGL()->DeleteVertexArrays             = (OpenglDeleteBuffers)PlatformGetGlFunction("glDeleteVertexArrays");
      GetGL()->Initialized               &= GetGL()->DeleteVertexArrays != 0;

      GetGL()->DeleteBuffers             = (OpenglDeleteBuffers)PlatformGetGlFunction("glDeleteBuffers");
      GetGL()->Initialized               &= GetGL()->DeleteBuffers != 0;

      GetGL()->GenBuffers                = (OpenglGenBuffers)PlatformGetGlFunction("glGenBuffers");
      GetGL()->Initialized               &= GetGL()->GenBuffers != 0;

      GetGL()->GenVertexArrays           = (OpenglGenVertexArrays)PlatformGetGlFunction("glGenVertexArrays");
      GetGL()->Initialized               &= GetGL()->GenVertexArrays != 0;

      GetGL()->BufferData                = (OpenglBufferData)PlatformGetGlFunction("glBufferData");
      GetGL()->Initialized               &= GetGL()->BufferData != 0;

      GetGL()->BufferSubData             = (OpenglBufferSubData)PlatformGetGlFunction("glBufferSubData");
      GetGL()->Initialized               &= GetGL()->BufferSubData != 0;


      GetGL()->MapBuffer                 = (OpenglMapBuffer)PlatformGetGlFunction("glMapBuffer");
      GetGL()->Initialized               &= GetGL()->MapBuffer != 0;

      GetGL()->MapBufferRange            = (OpenglMapBufferRange)PlatformGetGlFunction("glMapBufferRange");
      GetGL()->Initialized               &= GetGL()->MapBufferRange != 0;

      GetGL()->UnmapBuffer               = (OpenglUnmapBuffer)PlatformGetGlFunction("glUnmapBuffer");
      GetGL()->Initialized               &= GetGL()->UnmapBuffer != 0;

      GetGL()->DrawBuffers               = (OpenglDrawBuffers)PlatformGetGlFunction("glDrawBuffers");
      GetGL()->Initialized               &= GetGL()->DrawBuffers != 0;

      GetGL()->GetIntegerv               = (OpenglGetIntegerv)PlatformGetGlFunction("glGetIntegerv");
      GetGL()->Initialized               &= GetGL()->GetIntegerv != 0;


      GetGL()->Finish                    = (OpenglFinish)PlatformGetGlFunction("glFinish");
      GetGL()->Initialized               &= GetGL()->Finish != 0;

      GetGL()->GenQueries                = (OpenglGenQueries)PlatformGetGlFunction("glGenQueries");
      GetGL()->Initialized               &= GetGL()->GenQueries != 0;

      GetGL()->DeleteQueries                = (OpenglDeleteQueries)PlatformGetGlFunction("glDeleteQueries");
      GetGL()->Initialized               &= GetGL()->DeleteQueries != 0;

      GetGL()->BeginQuery                = (OpenglBeginQuery)PlatformGetGlFunction("glBeginQuery");
      GetGL()->Initialized               &= GetGL()->BeginQuery != 0;

      GetGL()->EndQuery                  = (OpenglEndQuery)PlatformGetGlFunction("glEndQuery");
      GetGL()->Initialized               &= GetGL()->EndQuery != 0;


      GetGL()->FenceSync                 = (OpenglFenceSync)PlatformGetGlFunction("glFenceSync");
      GetGL()->Initialized               &= GetGL()->FenceSync != 0;

      GetGL()->DeleteSync                = (OpenglDeleteSync)PlatformGetGlFunction("glDeleteSync");
      GetGL()->Initialized               &= GetGL()->DeleteSync != 0;

      /* GetGL()->WaitSync                  = (OpenglWaitSync)PlatformGetGlFunction("glWaitSync"); */
      /* GetGL()->Initialized               &= GetGL()->WaitSync != 0; */

      GetGL()->ClientWaitSync            = (OpenglClientWaitSync)PlatformGetGlFunction("glClientWaitSync");
      GetGL()->Initialized               &= GetGL()->ClientWaitSync != 0;




      GetGL()->GetQueryObjectiv          = (OpenglGetQueryObjectiv)PlatformGetGlFunction("glGetQueryObjectiv");
      GetGL()->Initialized               &= GetGL()->GetQueryObjectiv != 0;

      GetGL()->GetQueryObjectuiv         = (OpenglGetQueryObjectuiv)PlatformGetGlFunction("glGetQueryObjectuiv");
      GetGL()->Initialized               &= GetGL()->GetQueryObjectuiv != 0;

      GetGL()->GetQueryObjecti64v        = (OpenglGetQueryObjecti64v)PlatformGetGlFunction("glGetQueryObjecti64v");
      GetGL()->Initialized               &= GetGL()->GetQueryObjecti64v != 0;

      GetGL()->GetQueryObjectui64v       = (OpenglGetQueryObjectui64v)PlatformGetGlFunction("glGetQueryObjectui64v");
      GetGL()->Initialized               &= GetGL()->GetQueryObjectui64v != 0;






      GetGL()->GenerateMipmap            = (OpenglGenerateMipmap)PlatformGetGlFunction("glGenerateMipmap");
      GetGL()->Initialized               &= GetGL()->GenerateMipmap != 0;

      // NOTE(nsillik)(macos): glGenerateTextureMipmap is GL 4.5 / ARB_direct_state_access and is
      // absent on macOS' 4.1 core context.  Loaded but deliberately not required -- the one caller
      // falls back to glGenerateMipmap on the GL_TEXTURE_2D_ARRAY it has already bound.
      GetGL()->GenerateTextureMipmap     = (OpenglGenerateTextureMipmap)PlatformGetGlFunction("glGenerateTextureMipmap");



      GetGL()->GetIntegerv(GL_MAJOR_VERSION, &GLMajor);
      GetGL()->GetIntegerv(GL_MINOR_VERSION, &GLMinor);

      if (GLMajor >= 4)
      {
        GetGL()->BlendFunci                = (OpenglBlendFunci)PlatformGetGlFunction("glBlendFunci");
        GetGL()->Initialized              &= GetGL()->BlendFunci != 0;
      }

    }
    else
    {
      Warn("Invalid Opengl Driver Info : (%s) (%s) (%s)", Vendor, Renderer, Version);
      GetGL()->Initialized = False;
    }
  }


  if (GetGL()->Initialized)
  {
    /* GetGL()->DebugMessageCallback(HandleGlDebugMessage, 0); */
    /* GetGL()->Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS); */

    GetGL()->Enable(GL_DEPTH_TEST);

    GetGL()->DepthFunc(GL_LEQUAL);

    AssertNoGlErrors;
  }

  if (GetGL()->Initialized && CheckOpenglVersion(GLMajor, GLMinor) == False)
  {
    Warn("Unsupported Version of Opengl (%d.%d) ::  Minimum 3.3 required.", GLMajor, GLMinor);
    Warn("The driver successfully supplied all required function pointers, however your program may not run correctly.");
    Warn("If you experience issues, please upgrade to an OpenGL 3.3 compliant driver.");
  }

  AssertNoGlErrors;
  b32 Result = GetGL()->Initialized;
  return Result;
}

void
HandleGlDebugMessage(GLenum Source,
                     GLenum Type,
                     GLuint Id,
                     GLenum Severity,
                     GLsizei MessageLength,
                     const GLchar* Message,
                     const void* UserData)
{
  /* if (Severity != GL_DEBUG_SEVERITY_NOTIFICATION) */
  {

    /* GLInfo("%s", Message); */

    const char* MessageTypeName = 0;
    switch(Type) {
      case(GL_DEBUG_TYPE_ERROR):
      {
        MessageTypeName = "ERROR";
      } break;
      case(GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR):
      {
        MessageTypeName = "DEPRECATED_BEHAVIOR";
      } break;
      case(GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR):
      {
        MessageTypeName = "UNDEFINED_BEHAVIOR";
      } break;
      case(GL_DEBUG_TYPE_PORTABILITY):
      {
        MessageTypeName = "PORTABILITY";
      } break;
      case(GL_DEBUG_TYPE_PERFORMANCE):
      {
        MessageTypeName = "PERFORMANCE";
      } break;
      case(GL_DEBUG_TYPE_OTHER):
      {
        MessageTypeName = "OTHER";
      } break;
      InvalidDefaultCase;
    }

    OpenGlDebugMessage("Source %u, Type: %s, Id %u - %.*s", Source, MessageTypeName, Id, MessageLength, Message);
    if (UserData)
    {
      OpenGlDebugMessage("User Data At %p", UserData);
    }
  }

  return;
}

inline b32
BufferFloatDataToCard(u32 BufferId, u32 Stride, u32 ByteCount, void *Data, u32 *AttributeIndex)
{
#if BONSAI_DEBUG_SYSTEM_API
  debug_state *DebugState = GetDebugState();
  DebugState->BytesBufferedToCard += ByteCount;
#endif

  GetGL()->BindBuffer(GL_ARRAY_BUFFER, BufferId);
  AssertNoGlErrors;
  b32 BufferUnmapped = GetGL()->UnmapBuffer(GL_ARRAY_BUFFER);
  /* GetGL()->BufferData(GL_ARRAY_BUFFER, ByteCount, Data, GL_STATIC_DRAW); */
  AssertNoGlErrors;

  GetGL()->EnableVertexAttribArray(*AttributeIndex);
  AssertNoGlErrors;
  GetGL()->VertexAttribPointer(*AttributeIndex, (s32)Stride, GL_FLOAT, GL_FALSE, 0, (void*)0);
  *AttributeIndex += 1;
  AssertNoGlErrors;

  return BufferUnmapped;
}

template <typename T> inline b32
BufferVertsToCard(u32 BufferId, T *Mesh, u32 *AttributeIndex)
{
  TIMED_FUNCTION();
  u32 ByteCount = Mesh->At*sizeof(*Mesh->Verts);
  u32 Stride = sizeof(*Mesh->Verts)/sizeof(Mesh->Verts[0].E[0]);

  b32 Result = BufferFloatDataToCard(BufferId, Stride, ByteCount, (void*)Mesh->Verts, AttributeIndex);
  return Result;
}

#if 1
template <typename T> inline b32
BufferColorsToCard(u32 BufferId, T *Mesh, u32* AttributeIndex)
{
  TIMED_FUNCTION();
  u32 Stride = sizeof(*Mesh->Colors)/sizeof(Mesh->Colors[0].E[0]);
  u32 ByteCount = Mesh->At*sizeof(*Mesh->Colors);

  b32 Result = BufferFloatDataToCard(BufferId, Stride, ByteCount, (void*)Mesh->Colors, AttributeIndex);
  return Result;
}
#endif

template <typename T> inline b32
BufferNormalsToCard(u32 BufferId, T *Mesh, u32 *AttributeIndex)
{
  TIMED_FUNCTION();
  u32 Stride = sizeof(*Mesh->Normals)/sizeof(Mesh->Normals[0].E[0]);
  u32 ByteCount = Mesh->At*sizeof(*Mesh->Normals);

  b32 Result = BufferFloatDataToCard(BufferId, Stride, ByteCount, (void*)Mesh->Normals, AttributeIndex);
  return Result;
}

template <typename T> inline b32
BufferUVsToCard(u32 BufferId, T *Mesh, u32 *AttributeIndex)
{
  TIMED_FUNCTION();
  u32 ByteCount = Mesh->At*sizeof(*Mesh->UVs);
  u32 Stride = sizeof(*Mesh->UVs)/sizeof(Mesh->UVs[0].x);

  b32 Result = BufferFloatDataToCard(BufferId, Stride, ByteCount, (void*)Mesh->UVs, AttributeIndex);
  return Result;
}
