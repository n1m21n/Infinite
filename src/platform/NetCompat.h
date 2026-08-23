#pragma once

// BSD-sockets compatibility for the Windows build.
//
// RemoteControl (TCP JSON-RPC) and OscNodes (UDP) were written against the
// POSIX socket API. On Windows this header pulls in winsock2/ws2tcpip and
// papers over the small differences:
//
//   - NetClose(fd) maps to closesocket. This used to be `#define close(fd)
//     closesocket(fd)`, which was NOT safe: an unscoped macro named `close`
//     also hits <fstream>'s basic_filebuf::close() when a TU includes both
//     (RemoteControl.cpp does), mangling those declarations in that one TU
//     while every other TU in the same MSVC codegen batch sees the real
//     ones. That produced a stream of C4003 warnings and then an internal
//     compiler error during "Generating Code". Use the function.
//   - SHUT_RDWR -> SD_BOTH
//   - socklen_t doesn't exist on Windows
//   - WSAStartup must run before any socket call; the ProcessScope object
//     below does it once per translation unit that includes this header
//     (refcounted by the OS, safe to repeat).
//
// POSIX platforms get the real headers unchanged.

#if defined(_WIN32)
   #ifndef WIN32_LEAN_AND_MEAN
      #define WIN32_LEAN_AND_MEAN
   #endif
   #ifndef NOMINMAX
      #define NOMINMAX
   #endif
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #pragma comment(lib, "ws2_32.lib")

   typedef int socklen_t;
   typedef long NetSsize; // recv/send return int on Windows

   #ifndef SHUT_RDWR
      #define SHUT_RDWR SD_BOTH
   #endif

   namespace NetCompat
   {
      struct ProcessScope
      {
         ProcessScope()
         {
            WSADATA data;
            WSAStartup(MAKEWORD(2, 2), &data);
         }
      };
      // One instance per TU including this header. WSAStartup refcounts, so
      // repeats across TUs are fine and cleanup is deliberately skipped -
      // the socket layer lives as long as the process.
      inline ProcessScope gNetInit;

      // The Windows shapes take char*/int where POSIX takes void*/size_t;
      // these shims let call sites stay written against the POSIX forms.
      inline NetSsize NetRecv(int fd, void* buf, size_t len, int flags)
      {
         return ::recv(static_cast<SOCKET>(fd), static_cast<char*>(buf),
                       static_cast<int>(len), flags);
      }
      inline NetSsize NetSend(int fd, const void* buf, size_t len, int flags)
      {
         return ::send(static_cast<SOCKET>(fd), static_cast<const char*>(buf),
                       static_cast<int>(len), flags);
      }
      inline NetSsize NetSendTo(int fd, const void* buf, size_t len, int flags,
                                const sockaddr* to, socklen_t tolen)
      {
         return ::sendto(static_cast<SOCKET>(fd), static_cast<const char*>(buf),
                         static_cast<int>(len), flags, to, static_cast<int>(tolen));
      }
      inline int NetSetSockOpt(int fd, int level, int optname, const void* optval, socklen_t optlen)
      {
         return ::setsockopt(static_cast<SOCKET>(fd), level, optname,
                             static_cast<const char*>(optval), static_cast<int>(optlen));
      }
      inline int NetClose(int fd) { return ::closesocket(static_cast<SOCKET>(fd)); }
   }
#else
   #include <arpa/inet.h>
   #include <netinet/in.h>
   #include <sys/socket.h>
   #include <unistd.h>

   typedef ssize_t NetSsize;

   namespace NetCompat
   {
      inline NetSsize NetRecv(int fd, void* buf, size_t len, int flags) { return ::recv(fd, buf, len, flags); }
      inline NetSsize NetSend(int fd, const void* buf, size_t len, int flags) { return ::send(fd, buf, len, flags); }
      inline NetSsize NetSendTo(int fd, const void* buf, size_t len, int flags, const sockaddr* to, socklen_t tolen) { return ::sendto(fd, buf, len, flags, to, tolen); }
      inline int NetSetSockOpt(int fd, int level, int optname, const void* optval, socklen_t optlen) { return ::setsockopt(fd, level, optname, optval, optlen); }
      inline int NetClose(int fd) { return ::close(fd); }
   }
#endif
