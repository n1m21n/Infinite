// fork: Windows half of AgentNet.h (WinHTTP + Credential Manager). NOT VERIFIED on Windows: written from the API
// documentation, never compiled or run here.
#include "AgentNet.h"

#include <windows.h>
#include <wincred.h>
#include <winhttp.h>

namespace AgentNet
{
   namespace
   {
      std::wstring Wide(const std::string& s)
      {
         if (s.empty())
            return {};
         const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
         std::wstring w((size_t)n, L'\0');
         MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
         return w;
      }
   }

   bool Post(const Request& request, const std::function<bool(const char*, size_t)>& onData, int& status, std::string& error)
   {
      status = 0;
      error.clear();
      const std::wstring wurl = Wide(request.url);
      URL_COMPONENTSW uc{};
      uc.dwStructSize = sizeof uc;
      wchar_t host[256] = {}, path[2048] = {};
      uc.lpszHostName = host;
      uc.dwHostNameLength = 255;
      uc.lpszUrlPath = path;
      uc.dwUrlPathLength = 2047;
      if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc))
      {
         error = "the URL is not valid";
         return false;
      }
      HINTERNET session = WinHttpOpen(L"Infinite", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
      if (!session)
      {
         error = "WinHttpOpen failed";
         return false;
      }
      const DWORD ms = (DWORD)request.timeoutSeconds * 1000;
      WinHttpSetTimeouts(session, ms, ms, ms, ms);
      HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
      HINTERNET req = conn ? WinHttpOpenRequest(conn, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                            : nullptr;
      bool ok = false;
      if (req)
      {
         std::wstring headers;
         for (const auto& h : request.headers)
            headers += Wide(h.first) + L": " + Wide(h.second) + L"\r\n";
         if (WinHttpSendRequest(req, headers.c_str(), (DWORD)headers.size(), (LPVOID)request.body.data(), (DWORD)request.body.size(),
                                (DWORD)request.body.size(), 0) &&
             WinHttpReceiveResponse(req, nullptr))
         {
            DWORD code = 0, size = sizeof code;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            status = (int)code;
            ok = true;
            for (;;)
            {
               DWORD avail = 0;
               if (!WinHttpQueryDataAvailable(req, &avail))
               {
                  ok = false;
                  error = "the connection broke";
                  break;
               }
               if (avail == 0)
                  break;
               std::string buf(avail, '\0');
               DWORD got = 0;
               if (!WinHttpReadData(req, buf.data(), avail, &got))
               {
                  ok = false;
                  error = "the connection broke";
                  break;
               }
               if (!onData(buf.data(), got))
               {
                  ok = false;
                  error = "cancelled";
                  break;
               }
            }
         }
         else
            error = "could not reach the server (error " + std::to_string(GetLastError()) + ")";
      }
      else
         error = "could not open the request";
      if (req)
         WinHttpCloseHandle(req);
      if (conn)
         WinHttpCloseHandle(conn);
      WinHttpCloseHandle(session);
      return ok;
   }

   bool SecretSet(const std::string& service, const std::string& account, const std::string& value)
   {
      const std::wstring target = Wide(service + "/" + account);
      CREDENTIALW c{};
      c.Type = CRED_TYPE_GENERIC;
      c.TargetName = const_cast<LPWSTR>(target.c_str());
      c.CredentialBlobSize = (DWORD)value.size();
      c.CredentialBlob = (LPBYTE)value.data();
      c.Persist = CRED_PERSIST_LOCAL_MACHINE;
      return CredWriteW(&c, 0) != FALSE;
   }

   bool SecretGet(const std::string& service, const std::string& account, std::string& value)
   {
      PCREDENTIALW c = nullptr;
      if (!CredReadW(Wide(service + "/" + account).c_str(), CRED_TYPE_GENERIC, 0, &c) || c == nullptr)
         return false;
      value.assign((const char*)c->CredentialBlob, c->CredentialBlobSize);
      CredFree(c);
      return true;
   }

   bool SecretDelete(const std::string& service, const std::string& account)
   {
      return CredDeleteW(Wide(service + "/" + account).c_str(), CRED_TYPE_GENERIC, 0) != FALSE || GetLastError() == ERROR_NOT_FOUND;
   }

   bool SecretsAvailable() { return true; }
}
