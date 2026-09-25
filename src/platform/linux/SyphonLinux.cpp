#include "platform/Platform.h"

#include <string>
#include <vector>

namespace Platform
{
   struct SyphonServerHandle
   {
   };

   struct SyphonClientHandle
   {
   };

   SyphonServerHandle* SyphonServerCreate(const std::string& /*serverName*/)
   {
      return nullptr;
   }

   void SyphonServerUpdateName(SyphonServerHandle* /*handle*/, const std::string& /*serverName*/)
   {
   }

   void SyphonServerPublish(SyphonServerHandle* /*handle*/, unsigned int /*textureId*/,
                            int /*width*/, int /*height*/, bool /*flipped*/)
   {
   }

   bool SyphonServerHasClients(SyphonServerHandle* /*handle*/)
   {
      return false;
   }

   bool SyphonServerCanReportClients()
   {
      return false;
   }

   void SyphonServerDestroy(SyphonServerHandle* handle)
   {
      delete handle;
   }

   std::vector<SyphonServerInfo> SyphonGetAvailableServers()
   {
      return {};
   }

   SyphonClientHandle* SyphonClientCreate()
   {
      return nullptr;
   }

   bool SyphonClientConnect(SyphonClientHandle* /*handle*/, const std::string& /*appName*/,
                            const std::string& /*serverName*/, const std::string& /*uuid*/)
   {
      return false;
   }

   bool SyphonClientIsConnected(SyphonClientHandle* /*handle*/)
   {
      return false;
   }

   bool SyphonClientHasNewFrame(SyphonClientHandle* /*handle*/)
   {
      return false;
   }

   unsigned int SyphonClientGetFrameTexture(SyphonClientHandle* /*handle*/, int& outWidth, int& outHeight)
   {
      outWidth = 0;
      outHeight = 0;
      return 0;
   }

   void SyphonClientDestroy(SyphonClientHandle* handle)
   {
      delete handle;
   }
}
