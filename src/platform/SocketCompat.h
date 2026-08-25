#pragma once

#include <mutex>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using InfiniteSocket = SOCKET;
constexpr InfiniteSocket kInfiniteInvalidSocket = INVALID_SOCKET;

inline bool InfiniteSocketsReady()
{
   static std::once_flag once;
   static bool ready = false;
   std::call_once(once, [] {
      WSADATA data{};
      ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
   });
   return ready;
}

inline void InfiniteCloseSocket(InfiniteSocket socket)
{
   if (socket != kInfiniteInvalidSocket)
      closesocket(socket);
}

inline void InfiniteShutdownSocket(InfiniteSocket socket)
{
   if (socket != kInfiniteInvalidSocket)
      shutdown(socket, SD_BOTH);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using InfiniteSocket = int;
constexpr InfiniteSocket kInfiniteInvalidSocket = -1;

inline bool InfiniteSocketsReady() { return true; }
inline void InfiniteCloseSocket(InfiniteSocket socket) { if (socket >= 0) close(socket); }
inline void InfiniteShutdownSocket(InfiniteSocket socket) { if (socket >= 0) shutdown(socket, SHUT_RDWR); }
#endif
