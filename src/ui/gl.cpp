
link_internal void
FramebufferTextureLayer(framebuffer *FBO, texture *Tex, ui_texture_slice Layer)
{
  // NOTE(nsillik)(macos): Same counter, same hazard as FramebufferTexture -- see the note
  // on struct framebuffer.  Zero callers today, fixed so it cannot become one.
  u32 Attachment = 0;
  if (!NextFreeFramebufferAttachment(FBO, Tex, &Attachment)) { return; }

  FBO->AttachmentTextureIDs[FBO->Attachments++] = Tex->ID;
  GetGL()->FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + Attachment, Tex->ID, 0, Layer);
  return;
}

link_internal b32
MakeRenderToTextureShader(shader *Shader, memory_arena *Memory, m4 *ViewProjection, texture *ColorPalette)
{
  b32 Result = CompileShaderPair(Shader, CSz(BONSAI_SHADER_PATH "RenderToTexture.vertexshader"), CSz(BONSAI_SHADER_PATH "RenderToTexture.fragmentshader") );

  if (Result)
  {
    Shader->Uniforms = ShaderUniformBuffer(1, Memory);

    InitShaderUniform(Shader, 0, ViewProjection, "ViewProjection");
    /* InitShaderUniform(Shader, 1, ColorPalette,   "ColorPalette"); */
  }

  return Result;
}
