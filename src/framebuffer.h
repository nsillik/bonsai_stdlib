
// GL 4.1 guarantees at least 8 colour attachments; nothing in the tree uses more than two.
#define MAX_FRAMEBUFFER_ATTACHMENTS 8

struct framebuffer
{
  u32 ID;
  u32 Attachments;

  // NOTE(nsillik)(macos): Which image occupies each of the slots 0 .. Attachments-1.
  // Without it FramebufferTexture cannot tell that an image is already attached to this
  // framebuffer and attaches it a second time -- two draw buffers aliasing one image,
  // which Apple's GL answers by silently discarding every fragment of every draw into
  // that framebuffer: no GL error, GL_FRAMEBUFFER_COMPLETE, and Mesa renders it fine.
  u32 AttachmentTextureIDs[MAX_FRAMEBUFFER_ATTACHMENTS];
};


struct rtt_framebuffer
{
  framebuffer FBO;
      texture DestTexture;
};

poof(static_cursor(rtt_framebuffer, {3}))
#include <generated/static_cursor_rokjL8Dl.h>

poof(circular_buffer_h(rtt_framebuffer, {static_cursor_3}))
#include <generated/circular_buffer_h_Tg6yrcq1.h>

link_internal void
BindFramebuffer(rtt_framebuffer *Framebuffer);

link_internal void
ClearFramebuffer(rtt_framebuffer *Framebuffer);

link_internal framebuffer
GenFramebuffer();

link_internal void
SetDrawBuffers(framebuffer *FBO);

link_internal b32
InitializeRenderToTextureFramebuffer(rtt_framebuffer *Framebuffer, v2i Dim, cs DebugTextureName);

link_internal void
FramebufferTexture(framebuffer *FBO, texture *Tex);

link_internal void
FramebufferDepthTexture(texture *Tex);

// NOTE(nsillik): Returns False when Tex is already attached to FBO, which is a no-op for
// the caller rather than an error; see the note on struct framebuffer.
link_internal b32
NextFreeFramebufferAttachment(framebuffer *FBO, texture *Tex, u32 *AttachmentIndex);

