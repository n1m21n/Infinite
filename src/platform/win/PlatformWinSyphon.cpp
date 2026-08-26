// Windows counterpart to Platform.mm's Syphon section: implements the same
// Platform::Syphon* surface (declared in ../Platform.h) on top of Spout2's
// "SpoutGL" library instead of Syphon itself. See
// local-prompts/11-spout-windows.md for the design this follows.
//
// Kept out of PlatformWin.cpp because it needs SpoutGL's headers, which pull
// in DirectX11 and the legacy Microsoft <GL/gl.h> - and PlatformWin.cpp is
// also compiled into the infinite-vst3-scanner helper target, which has no
// GL/DX context and must stay untouched by any of that. This file is only
// added to the main Infinite target's sources (see CMakeLists.txt).
//
// Syphon returns GL_TEXTURE_RECTANGLE textures (macOS's native GL texture
// type); Spout returns GL_TEXTURE_2D. SyphonInNode.cpp's shader is hardcoded
// for sampler2DRect to match the documented Platform::SyphonClientGetFrameTexture
// contract, so rather than push that difference into node code (which would
// violate docs/CODE_STANDARDS.md #7 - platform-specific code stays behind
// Platform.h), the receive path here blits Spout's GL_TEXTURE_2D into an
// internally-owned GL_TEXTURE_RECTANGLE via SpoutGLBridge, keeping the
// contract literally true on Windows too.
//
// Identity mapping (see spec section 3): Syphon identifies a server by
// (appName, serverName, uuid); Spout senders have a single name string.
// serverName maps directly to the Spout sender name; appName is reported
// back as the constant "Spout"; uuid has no Spout equivalent and is ignored.

#include "../Platform.h"

#include "SpoutGLBridge.h"

#include "Spout.h"

namespace Platform
{
   struct SyphonServerHandle
   {
      Spout spout;
      std::string name;
   };

   struct SyphonClientHandle
   {
      Spout spout;
      unsigned int recvTex2D = 0;
      int recvW = 0;
      int recvH = 0;
      unsigned int rectFbo = 0;
      unsigned int rectTex = 0;
      int rectW = 0;
      int rectH = 0;
   };

   namespace
   {
      // Forces "GPU-interop-only" mode: Send/Receive calls fail closed
      // (return false) instead of silently falling back to a slower
      // DirectX-CPU-copy path when WGL_NV_DX_interop2 isn't available,
      // matching the spec's "fail softly, don't crash" requirement without
      // hand-rolling WGL extension detection ourselves.
      void ForceGpuInteropOnly(Spout& spout)
      {
         spout.SetAutoShare(false);
         spout.SetCPUshare(false);
      }
   }

   SyphonServerHandle* SyphonServerCreate(const std::string& serverName)
   {
      auto* handle = new SyphonServerHandle();
      handle->name = serverName.empty() ? "Spout" : serverName;
      ForceGpuInteropOnly(handle->spout);
      handle->spout.SetSenderName(handle->name.c_str());
      return handle;
   }

   void SyphonServerUpdateName(SyphonServerHandle* handle, const std::string& serverName)
   {
      if (handle == nullptr)
         return;
      handle->spout.ReleaseSender();
      handle->name = serverName.empty() ? "Spout" : serverName;
      handle->spout.SetSenderName(handle->name.c_str());
   }

   void SyphonServerPublish(SyphonServerHandle* handle, unsigned int textureId, int width, int height, bool flipped)
   {
      if (handle == nullptr || textureId == 0 || width <= 0 || height <= 0)
         return;

      // SendTexture's own default (bInvert=true) is the "normal" upright
      // orientation for a plain OpenGL texture; `flipped` inverts that.
      handle->spout.SendTexture(textureId, GL_TEXTURE_2D, (unsigned int)width, (unsigned int)height, !flipped);
   }

   bool SyphonServerHasClients(SyphonServerHandle* handle)
   {
      // Spout deliberately doesn't track receiver count (a receiver just
      // polls shared memory; there's no connection handshake on the sender
      // side to count) - IsInitialized() is the closest available proxy,
      // meaning this reports "publishing has started" rather than "someone
      // is actually receiving."
      return handle != nullptr && handle->spout.IsInitialized();
   }

   void SyphonServerDestroy(SyphonServerHandle* handle)
   {
      delete handle;
   }

   std::vector<SyphonServerInfo> SyphonGetAvailableServers()
   {
      std::vector<SyphonServerInfo> result;
      Spout enumerator; // shared-memory read only, no GPU/interop cost
      for (const std::string& name : enumerator.GetSenderList())
      {
         SyphonServerInfo info;
         info.appName = "Spout";
         info.serverName = name;
         result.push_back(std::move(info));
      }
      return result;
   }

   SyphonClientHandle* SyphonClientCreate()
   {
      return new SyphonClientHandle();
   }

   bool SyphonClientConnect(SyphonClientHandle* handle, const std::string&, const std::string& serverName, const std::string&)
   {
      if (handle == nullptr)
         return false;

      handle->spout.ReleaseReceiver();
      SpoutGLBridge::DeleteFbo(handle->rectFbo);
      SpoutGLBridge::DeleteTexture(handle->rectTex);
      SpoutGLBridge::DeleteTexture(handle->recvTex2D);
      handle->recvW = handle->recvH = handle->rectW = handle->rectH = 0;

      // Mirror Syphon's own SyphonClientConnect, which fails immediately if
      // no matching server exists rather than waiting for one to appear.
      if (!serverName.empty())
      {
         if (handle->spout.GetSenderIndex(serverName.c_str()) < 0)
            return false;
      }
      else if (handle->spout.GetSenderCount() == 0)
      {
         return false;
      }

      ForceGpuInteropOnly(handle->spout);
      handle->spout.SetReceiverName(serverName.empty() ? nullptr : serverName.c_str());
      return true;
   }

   bool SyphonClientIsConnected(SyphonClientHandle* handle)
   {
      return handle != nullptr && handle->spout.IsConnected();
   }

   bool SyphonClientHasNewFrame(SyphonClientHandle* handle)
   {
      return handle != nullptr && handle->spout.IsFrameNew();
   }

   unsigned int SyphonClientGetFrameTexture(SyphonClientHandle* handle, int& outWidth, int& outHeight)
   {
      outWidth = 0;
      outHeight = 0;
      if (handle == nullptr)
         return 0;

      if (handle->recvTex2D == 0)
         SpoutGLBridge::EnsureReceiveTexture(handle->recvTex2D, handle->recvW, handle->recvH, 1, 1);

      if (!handle->spout.ReceiveTexture(handle->recvTex2D, GL_TEXTURE_2D))
         return 0;

      if (handle->spout.IsUpdated())
      {
         // Sender's dimensions changed. This is Spout's documented
         // one-frame-lag usage (see its own ofApp.cpp receiver example),
         // not a bug: the ReceiveTexture call above skipped the actual copy
         // when it detected the change, so we reallocate here and the copy
         // happens on the *next* call.
         unsigned int w = handle->spout.GetSenderWidth();
         unsigned int h = handle->spout.GetSenderHeight();
         if (w > 0 && h > 0)
            SpoutGLBridge::EnsureReceiveTexture(handle->recvTex2D, handle->recvW, handle->recvH, (int)w, (int)h);
         return 0;
      }

      if (handle->recvW <= 0 || handle->recvH <= 0)
         return 0;

      if (!SpoutGLBridge::EnsureRectangleTarget(handle->rectFbo, handle->rectTex, handle->rectW, handle->rectH, handle->recvW, handle->recvH))
         return 0;

      if (!SpoutGLBridge::BlitToRectangle(handle->rectFbo, handle->recvTex2D, handle->recvW, handle->recvH))
         return 0;

      outWidth = handle->rectW;
      outHeight = handle->rectH;
      return handle->rectTex;
   }

   void SyphonClientDestroy(SyphonClientHandle* handle)
   {
      if (handle == nullptr)
         return;
      handle->spout.ReleaseReceiver();
      SpoutGLBridge::DeleteFbo(handle->rectFbo);
      SpoutGLBridge::DeleteTexture(handle->rectTex);
      SpoutGLBridge::DeleteTexture(handle->recvTex2D);
      delete handle;
   }
}
