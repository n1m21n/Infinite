#include "RemoteControl.h"

#include <cstdlib>
#include <deque>
#include <fstream>
#include <future>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include "platform/SettingsPaths.h"
#include "platform/SocketCompat.h"

using json = nlohmann::json;

namespace RemoteControl
{
   namespace
   {
      struct PendingCommand
      {
         std::string method;
         json params;
         std::string token;
         std::promise<json> response; // JSON-RPC "result" or throws-carried error, see below
      };

      std::mutex gQueueMutex;
      std::deque<PendingCommand*> gQueue;

      std::string gToken;
      bool gStarted = false;

      std::string SettingsDir()
      {
         return InfiniteSettingsDirectory();
      }

      std::string GenerateToken()
      {
         static const char kHex[] = "0123456789abcdef";
         std::random_device rd;
         std::mt19937_64 gen(rd());
         std::uniform_int_distribution<int> dist(0, 15);
         std::string token;
         token.reserve(32);
         for (int i = 0; i < 32; ++i)
            token += kHex[dist(gen)];
         return token;
      }

      void WriteTokenFile(const std::string& token)
      {
         const std::string dir = SettingsDir();
         if (dir.empty())
            return;
         std::ofstream file(dir + "/control_token");
         file << token;
      }

      // Handles one client connection to completion (reads/replies until the
      // socket closes or a fatal error). Runs on the accept thread - a single
      // client (the MCP server) is the expected case, so connections are
      // handled sequentially rather than one thread per client.
      void ServeConnection(InfiniteSocket fd, const Handler* handlerHolder)
      {
         (void)handlerHolder;
         std::string buffer;
         char chunk[4096];
         for (;;)
         {
            const int n = recv(fd, chunk, static_cast<int>(sizeof(chunk)), 0);
            if (n <= 0)
               break; // closed or error

            buffer.append(chunk, static_cast<size_t>(n));

            size_t newlinePos;
            while ((newlinePos = buffer.find('\n')) != std::string::npos)
            {
               std::string line = buffer.substr(0, newlinePos);
               buffer.erase(0, newlinePos + 1);
               if (line.empty())
                  continue;

               json request;
               json id = nullptr;
               json responseEnvelope;
               try
               {
                  request = json::parse(line);
                  id = request.value("id", json(nullptr));

                  auto* cmd = new PendingCommand();
                  cmd->method = request.value("method", std::string());
                  cmd->params = request.value("params", json::object());
                  cmd->token = request.value("token", std::string());

                  std::future<json> fut = cmd->response.get_future();
                  {
                     std::lock_guard<std::mutex> lock(gQueueMutex);
                     gQueue.push_back(cmd);
                  }
                  json result = fut.get(); // main thread fulfils this once per frame

                  responseEnvelope = { {"jsonrpc", "2.0"}, {"id", id} };
                  if (result.contains("__error__"))
                     responseEnvelope["error"] = { {"code", -32000}, {"message", result["__error__"]} };
                  else
                     responseEnvelope["result"] = result;
               }
               catch (const std::exception& e)
               {
                  responseEnvelope = { {"jsonrpc", "2.0"}, {"id", id},
                                       {"error", { {"code", -32700}, {"message", e.what()} } } };
               }

               // Replace, not throw, on bytes that are not UTF-8 (a path or a
               // text setting): an exception here would end the process.
               std::string out = responseEnvelope.dump(-1, ' ', false, json::error_handler_t::replace);
               out += "\n";
               // Large replies (explain, patch text) can need several sends.
               size_t sent = 0;
               while (sent < out.size())
               {
                  const int n = send(fd, out.data() + sent, static_cast<int>(out.size() - sent), 0);
                  if (n <= 0)
                     break;
                  sent += static_cast<size_t>(n);
               }
            }
         }
         InfiniteCloseSocket(fd);
      }

      void AcceptLoop(int port)
      {
         if (!InfiniteSocketsReady())
            return;
         InfiniteSocket listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
         if (listenFd == kInfiniteInvalidSocket)
            return;

         // Turbo 0.45: exclusive, not SO_REUSEADDR - on Windows that let a
         // second Infinite bind the same port and steal half the requests.
         int yes = 1;
         setsockopt(listenFd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                    reinterpret_cast<const char*>(&yes), static_cast<int>(sizeof(yes)));

         sockaddr_in addr{};
         addr.sin_family = AF_INET;
         addr.sin_port = htons(static_cast<uint16_t>(port));
         addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // loopback only - never 0.0.0.0

         if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
         {
            InfiniteCloseSocket(listenFd);
            return;
         }
         if (listen(listenFd, 4) != 0)
         {
            InfiniteCloseSocket(listenFd);
            return;
         }
         // Only the instance that owns the port publishes its token (a second
         // instance used to overwrite it and lock clients out of the first).
         WriteTokenFile(gToken);

         for (;;)
         {
            InfiniteSocket clientFd = accept(listenFd, nullptr, nullptr);
            if (clientFd == kInfiniteInvalidSocket)
               continue;
            // Turbo 0.45: one thread per client, so a second MCP client (two
            // Claude windows) is not stuck behind the first one's connection.
            std::thread(ServeConnection, clientFd, nullptr).detach();
         }
      }
   }

   void Start(int port)
   {
      if (gStarted)
         return;
      gStarted = true;

      gToken = GenerateToken();

      std::thread(AcceptLoop, port).detach();
   }

   void DrainPending(const Handler& handler)
   {
      std::deque<PendingCommand*> batch;
      {
         std::lock_guard<std::mutex> lock(gQueueMutex);
         batch.swap(gQueue);
      }

      for (PendingCommand* cmd : batch)
      {
         json result;
         if (cmd->token != gToken)
         {
            json err;
            err["__error__"] = "invalid or missing auth token";
            cmd->response.set_value(err);
         }
         else
         {
            std::string error;
            json out;
            if (handler(cmd->method, cmd->params, out, error))
            {
               cmd->response.set_value(out);
            }
            else
            {
               json err;
               err["__error__"] = error.empty() ? std::string("unknown error") : error;
               cmd->response.set_value(err);
            }
         }
         delete cmd;
      }
   }
}
