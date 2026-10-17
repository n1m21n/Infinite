// Infinite-Turbo 0.47: the AI chat window (top bar button, Shift+C). Runs the
// user's own Claude Code CLI or, since 0.59, OpenAI's Codex CLI (logged in
// with a ChatGPT account), each with this app's MCP server, so the assistant
// edits the open patch with the same tools as in Claude Desktop or ChatGPT.
// Included by main.cpp inside its anonymous namespace; backend in
// platform/ClaudeChat*.
//
// 0.59 redesign: backend switch, guided setup when the CLI is missing or not
// logged in, attachments (file button, pasted screenshots, files dropped on
// the window) with image thumbnails, message bubbles, tool calls folded into
// one line per answer, an input that grows with its text and empties on send.

struct ChatEntry
{
   enum Role { kUser, kAssistant, kTool, kInfo, kError } role = kInfo;
   std::string text;
   std::vector<std::string> attachments; // user turns: the files sent with it
};

bool gChatOpen = false;
std::vector<ChatEntry> gChatLog;
std::string gChatSession;
std::string gChatInput; // Turbo 0.56: growable (a 4 KB buffer refused long pastes)
int gChatModel = 0;
bool gChatScroll = false;
bool gChatFocusInput = false;
double gChatStarted = 0.0;
std::string gChatClaudePath; // cached FindCli(); re-checked on demand
bool gChatClaudeChecked = false;
int gChatBackend = -1; // ClaudeChat::Backend, -1 until loaded from the settings folder
bool gChatShowSetup = false;
std::vector<std::string> gChatAttachments; // waiting to go with the next message
std::string gChatNotice;                   // one-line hint under the input (paste failed ...)
ImVec4 gChatRect(0, 0, 0, 0);              // window rect last frame (x0, y0, x1, y1), for drops
bool gChatSelectMode = false;              // the log as selectable text (Select button)
std::string gChatSelectBuf;                // that text, wrapped to the window width
size_t gChatSelectStamp = 0;               // what the buffer was built from (rebuild on change)

// The conversation as plain text (Copy all, and the Select view).
std::string ChatTranscript()
{
   std::string out;
   const char* bot = gChatBackend == ClaudeChat::kCodex ? "ChatGPT" : "Claude";
   for (const ChatEntry& e : gChatLog)
   {
      if (!out.empty())
         out += "\n\n";
      switch (e.role)
      {
         case ChatEntry::kUser: out += "You: " + e.text; break;
         case ChatEntry::kAssistant: out += std::string(bot) + ": " + e.text; break;
         case ChatEntry::kTool: out += "  [action] " + e.text; break;
         case ChatEntry::kError: out += "[error] " + e.text; break;
         case ChatEntry::kInfo: out += "(" + e.text + ")"; break;
      }
      for (const std::string& a : e.attachments)
         out += "\n  [attached] " + a;
   }
   return out;
}

// Hard-wraps `text` at `width` pixels for a read-only text field (ImGui
// 1.90's InputText does not wrap): line breaks are added only where a line
// would overflow.
std::string ChatWrapForField(const std::string& text, float width)
{
   ImFont* font = ImGui::GetFont();
   const float scale = ImGui::GetFontSize() / font->FontSize;
   std::string out;
   out.reserve(text.size() + text.size() / 40);
   size_t lineStart = 0;
   while (lineStart <= text.size())
   {
      size_t lineEnd = text.find('\n', lineStart);
      if (lineEnd == std::string::npos)
         lineEnd = text.size();
      const char* s = text.c_str() + lineStart;
      const char* e = text.c_str() + lineEnd;
      if (s == e)
         out += '\n';
      while (s < e)
      {
         const char* brk = font->CalcWordWrapPositionA(scale, s, e, width);
         if (brk <= s)
            brk = s + 1;
         out.append(s, brk);
         out += '\n';
         s = brk;
         while (s < e && *s == ' ')
            s++;
      }
      lineStart = lineEnd + 1;
   }
   if (!out.empty())
      out.pop_back();
   return out;
}

const std::vector<std::string>& ChatModelNames()
{
   static const std::vector<std::string> claude = { "default model", "sonnet", "opus", "haiku" };
   static const std::vector<std::string> codex = { "default model" };
   return gChatBackend == ClaudeChat::kCodex ? codex : claude;
}

const char* ChatBackendLabel(int b) { return b == ClaudeChat::kCodex ? "ChatGPT (Codex)" : "Claude"; }

const char* kChatSystemPrompt =
   "You are running inside Infinite-Turbo, a node-based audiovisual patching app, through its built-in chat "
   "window. The user is looking at the app. Use the infinite-turbo MCP tools to inspect and change the patch "
   "that is open: explain shows the live graph; call authoring_guide once before building something new; "
   "screenshot_node and render_frame let you look at the picture; drum_pattern loads grooves (A verse, "
   "B bridge, C chorus). Do the work with the tools instead of describing it. When the user attaches files, "
   "open each one first (images are visual references to follow, .inf files are patches). Reply in the "
   "user's language, briefly, as plain text (no tables, no headings). Every change is undoable with Ctrl+Z "
   "in the app.";

std::string ChatSettingsFile(const char* name)
{
   const std::string dir = InfiniteSettingsDirectory();
   return (dir.empty() ? std::string(".") : dir) + "/" + name;
}

void ChatLoadBackend()
{
   if (gChatBackend >= 0)
      return;
   gChatBackend = ClaudeChat::kClaude;
   std::ifstream in(std::filesystem::u8path(ChatSettingsFile("chat_backend")));
   int b = 0;
   if (in >> b && b == ClaudeChat::kCodex)
      gChatBackend = ClaudeChat::kCodex;
}

void ChatSetBackend(int b)
{
   if (b == gChatBackend || ClaudeChat::Busy())
      return;
   gChatBackend = b;
   std::ofstream out(std::filesystem::u8path(ChatSettingsFile("chat_backend")), std::ios::trunc);
   out << b << "\n";
   // Sessions do not carry across CLIs: start clean.
   ClaudeChat::Discard();
   gChatSession.clear();
   gChatModel = 0;
   gChatClaudePath = ClaudeChat::FindCli((ClaudeChat::Backend)gChatBackend);
   gChatClaudeChecked = true;
   gChatShowSetup = gChatClaudePath.empty() || !ClaudeChat::LooksLoggedIn((ClaudeChat::Backend)gChatBackend);
   gChatLog.push_back({ ChatEntry::kInfo, std::string("now talking to ") + ChatBackendLabel(b) + " (new conversation)" });
   gChatScroll = true;
}

bool ChatIsImagePath(const std::string& path)
{
   std::string ext = std::filesystem::u8path(path).extension().u8string();
   for (char& c : ext)
      c = (char)std::tolower((unsigned char)c);
   return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif" || ext == ".webp";
}

void ChatAddAttachment(const std::string& path)
{
   if (path.empty())
      return;
   if (std::find(gChatAttachments.begin(), gChatAttachments.end(), path) == gChatAttachments.end() &&
       gChatAttachments.size() < 8)
      gChatAttachments.push_back(path);
   gChatFocusInput = true;
}

// Files dropped while the pointer is over the chat window become
// attachments instead of nodes on the canvas (called from the drop handler).
bool ChatTakeDrops(const std::vector<std::string>& files, ImVec2 pos)
{
   if (!gChatOpen || files.empty() || pos.x < gChatRect.x || pos.x > gChatRect.z || pos.y < gChatRect.y ||
       pos.y > gChatRect.w)
      return false;
   for (const std::string& f : files)
      ChatAddAttachment(f);
   return true;
}

bool ChatPasteImage()
{
   const std::string dir = ChatSettingsFile("chat") + "/attachments";
   char name[64];
   snprintf(name, sizeof(name), "pasted-%lld.png", (long long)std::time(nullptr));
   const std::string path = dir + "/" + name;
   std::string error;
   if (!ClaudeChat::SaveClipboardImage(path, error))
   {
      gChatNotice = error;
      return false;
   }
   ChatAddAttachment(path);
   gChatNotice.clear();
   return true;
}

// Small GL thumbnails of attached images, loaded once per path.
struct ChatThumb
{
   unsigned int tex = 0;
   int w = 0, h = 0;
   bool failed = false;
};
std::map<std::string, ChatThumb> gChatThumbs;

const ChatThumb& ChatThumbFor(const std::string& path)
{
   ChatThumb& t = gChatThumbs[path];
   if (t.tex != 0 || t.failed)
      return t;
   // From memory: the app's stb_image is built with STBI_NO_STDIO (no
   // stbi_load), and reading through u8path also takes non-ASCII paths.
   std::vector<unsigned char> bytes;
   {
      std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
      bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
   }
   int w = 0, h = 0, n = 0;
   unsigned char* px = bytes.empty() ? nullptr
                                     : stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
   if (px == nullptr || w <= 0 || h <= 0)
   {
      if (px)
         stbi_image_free(px);
      t.failed = true;
      return t;
   }
   // Downscale to at most 128 px on the long side (nearest), plenty for a chip.
   const int maxSide = 128;
   const float sc = std::min(1.0f, (float)maxSide / (float)std::max(w, h));
   const int tw = std::max(1, (int)(w * sc)), th = std::max(1, (int)(h * sc));
   std::vector<unsigned char> small((size_t)tw * (size_t)th * 4);
   for (int y = 0; y < th; y++)
      for (int x = 0; x < tw; x++)
      {
         const int sx = std::min(w - 1, (int)(x / sc)), sy = std::min(h - 1, (int)(y / sc));
         std::memcpy(&small[((size_t)y * tw + x) * 4], &px[((size_t)sy * w + sx) * 4], 4);
      }
   stbi_image_free(px);
   glGenTextures(1, &t.tex);
   glBindTexture(GL_TEXTURE_2D, t.tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, small.data());
   glBindTexture(GL_TEXTURE_2D, 0);
   t.w = tw;
   t.h = th;
   return t;
}

void ChatAppend(ChatEntry::Role role, const std::string& text)
{
   // Consecutive assistant blocks read as one message.
   if (role == ChatEntry::kAssistant && !gChatLog.empty() && gChatLog.back().role == ChatEntry::kAssistant)
      gChatLog.back().text += "\n\n" + text;
   else
      gChatLog.push_back({ role, text, {} });
   gChatScroll = true;
}

std::string ChatStripMarkdown(const std::string& in)
{
   // Light clean-up: drop ** and __ emphasis and leading #s, keep the text.
   std::string out;
   out.reserve(in.size());
   bool lineStart = true;
   for (size_t i = 0; i < in.size(); i++)
   {
      if ((in[i] == '*' || in[i] == '_') && i + 1 < in.size() && in[i + 1] == in[i])
      {
         i++;
         continue;
      }
      if (lineStart && in[i] == '#')
      {
         while (i < in.size() && in[i] == '#')
            i++;
         if (i < in.size() && in[i] == ' ')
            continue;
         i--;
         continue;
      }
      out.push_back(in[i]);
      lineStart = in[i] == '\n';
   }
   return out;
}

void ChatPump()
{
   std::vector<ClaudeChat::Event> events;
   ClaudeChat::Drain(events);
   for (ClaudeChat::Event& e : events)
   {
      switch (e.kind)
      {
         case ClaudeChat::Event::kText: ChatAppend(ChatEntry::kAssistant, ChatStripMarkdown(e.text)); break;
         case ClaudeChat::Event::kTool: ChatAppend(ChatEntry::kTool, e.text); break;
         case ClaudeChat::Event::kToolError: ChatAppend(ChatEntry::kError, "tool error: " + e.text); break;
         case ClaudeChat::Event::kSession: gChatSession = e.text; break;
         case ClaudeChat::Event::kInfo: break; // model line: shown in the status, not the log
         case ClaudeChat::Event::kError:
         {
            std::string text = e.text;
            if (text.find("login") != std::string::npos || text.find("Login") != std::string::npos ||
                text.find("authenticat") != std::string::npos || text.find("401") != std::string::npos)
            {
               text += "\n\nYou are probably not logged in: open the setup (the ? button above) and use Log in.";
               gChatShowSetup = true;
            }
            ChatAppend(ChatEntry::kError, text);
            break;
         }
         case ClaudeChat::Event::kDone:
            if (!e.text.empty())
               ChatAppend(ChatEntry::kInfo, e.text);
            break;
      }
   }
}

// Copies the attachments into the CLI's work folder (it may read only there)
// and lists them for the model.
std::string ChatStageAttachments(const std::string& workDir, std::vector<std::string>& staged)
{
   namespace fs = std::filesystem;
   std::error_code ec;
   const fs::path dir = fs::u8path(workDir) / "attachments";
   fs::create_directories(dir, ec);
   std::string list;
   for (const std::string& src : gChatAttachments)
   {
      const fs::path from = fs::u8path(src);
      fs::path to = dir / from.filename();
      if (fs::absolute(from, ec) != fs::absolute(to, ec))
      {
         int n = 1;
         while (fs::exists(to, ec) && n < 1000)
            to = dir / (from.stem().u8string() + "-" + std::to_string(n++) + from.extension().u8string());
         fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
         if (ec)
            to = from; // copy failed: point at the original
      }
      staged.push_back(to.u8string());
      list += "\n- " + to.u8string() + (ChatIsImagePath(src) ? "  (image: a visual reference)" : "");
   }
   return list;
}

void ChatSend()
{
   std::string prompt = gChatInput;
   while (!prompt.empty() && (prompt.back() == '\n' || prompt.back() == '\r' || prompt.back() == ' ' || prompt.back() == '\t'))
      prompt.pop_back();
   if ((prompt.empty() && gChatAttachments.empty()) || ClaudeChat::Busy())
      return;
   if (prompt.empty())
      prompt = "Look at the attached file(s) and tell me what you see and how it could become a patch.";
   const ClaudeChat::Backend backend = (ClaudeChat::Backend)gChatBackend;
   if (gChatClaudePath.empty())
      gChatClaudePath = ClaudeChat::FindCli(backend);
   ClaudeChat::Options opt;
   opt.backend = backend;
   opt.claudePath = gChatClaudePath;
   opt.exePath = Platform::ExecutablePath();
   const std::string settingsDir = InfiniteSettingsDirectory();
   opt.workDir = settingsDir.empty() ? std::string(".") : (std::filesystem::u8path(settingsDir) / "chat").u8string();
   opt.sessionId = gChatSession;
   opt.model = gChatModel > 0 && gChatModel < (int)ChatModelNames().size() ? ChatModelNames()[gChatModel] : std::string();
   opt.systemPrompt = kChatSystemPrompt;

   std::vector<std::string> staged;
   std::string full = prompt;
   if (!gChatAttachments.empty())
   {
      const std::string list = ChatStageAttachments(opt.workDir, staged);
      full += "\n\n[Attached files - open each one before answering:" + list + "]";
      if (backend == ClaudeChat::kCodex)
         for (const std::string& f : staged)
            if (ChatIsImagePath(f))
               opt.images.push_back(f);
   }
   // Codex has no system prompt flag: the first turn of a conversation carries it.
   if (backend == ClaudeChat::kCodex && gChatSession.empty())
      full = std::string("[Context] ") + kChatSystemPrompt + "\n\n[User] " + full;

   std::string error;
   gChatLog.push_back({ ChatEntry::kUser, prompt, gChatAttachments });
   gChatScroll = true;
   if (!ClaudeChat::Send(full, opt, error))
   {
      ChatAppend(ChatEntry::kError, error);
      return;
   }
   // The field may still be the active widget, and an active InputText keeps
   // (and writes back) its own copy of the text: release it before clearing,
   // or the sent text reappears.
   ImGui::ClearActiveID();
   gChatInput.clear();
   gChatAttachments.clear();
   gChatNotice.clear();
   gChatStarted = glfwGetTime();
}

// Guided setup: what is missing, with buttons that open a Command Prompt
// already running the right command.
void DrawChatSetup()
{
   const ClaudeChat::Backend backend = (ClaudeChat::Backend)gChatBackend;
   const bool codex = backend == ClaudeChat::kCodex;
   const bool installed = !gChatClaudePath.empty();
   const bool loggedIn = installed && ClaudeChat::LooksLoggedIn(backend);
   const ImVec4 ok(0.45f, 0.85f, 0.5f, 1.0f), todo(0.95f, 0.75f, 0.35f, 1.0f);

   ImGui::TextWrapped(codex ? "Chat with ChatGPT through OpenAI's Codex CLI, logged in with your own ChatGPT account "
                              "(Plus / Pro / Business), so it costs nothing beyond your plan."
                            : "Chat with Claude through Claude Code, the command-line Claude, logged in with your own "
                              "Claude account (Pro / Max), so it costs nothing beyond your plan.");
   ImGui::Spacing();

   auto step = [&](int n, bool done, const char* title) {
      ImGui::TextColored(done ? ok : todo, "%s", done ? "[ok]" : "[  ]");
      ImGui::SameLine();
      ImGui::Text("%d. %s", n, title);
   };
   auto commandRow = [&](const char* id, const char* cmd, const char* runLabel) {
      ImGui::Indent();
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputText(id, (char*)cmd, strlen(cmd) + 1, ImGuiInputTextFlags_ReadOnly);
      if (ImGui::SmallButton((std::string("copy") + id).c_str()))
         ImGui::SetClipboardText(cmd);
      ImGui::SameLine();
      if (ImGui::SmallButton((std::string(runLabel) + id).c_str()))
         ClaudeChat::OpenTerminal(cmd);
      ImGui::Unindent();
   };

   if (codex)
   {
      step(1, installed, "Install the Codex CLI (needs Node.js)");
      if (!installed)
      {
         ImGui::Indent();
         ImGui::TextDisabled("No Node.js yet? First: winget install OpenJS.NodeJS.LTS");
         ImGui::Unindent();
         commandRow("##codexinstall", "npm install -g @openai/codex", "run in a terminal");
      }
      step(2, loggedIn, "Log in with your ChatGPT account");
      if (installed && !loggedIn)
         commandRow("##codexlogin", "codex login", "log in");
   }
   else
   {
      step(1, installed, "Install Claude Code");
      if (!installed)
         commandRow("##claudeinstall",
                    "curl -fsSL https://claude.ai/install.cmd -o install.cmd && install.cmd && del install.cmd",
                    "run in a terminal");
      step(2, loggedIn, "Log in with your Claude account");
      if (installed && !loggedIn)
         commandRow("##claudelogin", "claude", "log in");
   }
   step(3, installed && loggedIn, "Come back here and check");
   ImGui::Indent();
   if (ImGui::Button("Check again"))
   {
      gChatClaudePath = ClaudeChat::FindCli(backend);
      gChatClaudeChecked = true;
      if (!gChatClaudePath.empty() && ClaudeChat::LooksLoggedIn(backend))
         gChatShowSetup = false;
   }
   if (installed)
   {
      ImGui::SameLine();
      if (ImGui::Button("Go to the chat"))
         gChatShowSetup = false;
   }
   ImGui::Unindent();
   ImGui::Spacing();
   ImGui::TextDisabled(codex ? "Found: %s" : "Found: %s", installed ? gChatClaudePath.c_str() : "nothing yet");
   ImGui::TextDisabled(codex ? "(or set INFINITE_CODEX_PATH to codex.exe)" : "(or set INFINITE_CLAUDE_PATH to claude.exe)");
   ImGui::Spacing();
   ImGui::TextWrapped("Other apps (Claude Desktop, ChatGPT on the web, Cursor...) can drive Infinite too: "
                      "Setup > AI connections.");
}

void DrawChatAttachmentChips(const std::vector<std::string>& files, bool removable)
{
   const float h = ImGui::GetFrameHeight() * 1.6f;
   int removeAt = -1;
   for (size_t i = 0; i < files.size(); i++)
   {
      ImGui::PushID((int)i);
      if (i > 0)
         ImGui::SameLine();
      const std::string name = std::filesystem::u8path(files[i]).filename().u8string();
      ImGui::BeginGroup();
      if (ChatIsImagePath(files[i]))
      {
         const ChatThumb& t = ChatThumbFor(files[i]);
         if (t.tex != 0)
         {
            const float w = h * (float)t.w / (float)std::max(1, t.h);
            ImGui::Image((ImTextureID)(intptr_t)t.tex, ImVec2(std::min(w, h * 2.0f), h));
            if (ImGui::IsItemHovered())
               ImGui::SetTooltip("%s", files[i].c_str());
         }
         else
            ImGui::Button(name.c_str());
      }
      else
      {
         ImGui::Button(name.c_str(), ImVec2(0, h));
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", files[i].c_str());
      }
      if (removable)
      {
         ImGui::SameLine(0.0f, 2.0f);
         if (ImGui::SmallButton("x"))
            removeAt = (int)i;
      }
      ImGui::EndGroup();
      ImGui::PopID();
   }
   if (removeAt >= 0)
      gChatAttachments.erase(gChatAttachments.begin() + removeAt);
}

void DrawClaudeChat()
{
   ChatPump(); // also while closed, so a turn finishing in the background lands in the log
   if (!gChatOpen)
   {
      gChatRect = ImVec4(0, 0, 0, 0);
      return;
   }
   ChatLoadBackend();
   if (!gChatClaudeChecked)
   {
      gChatClaudePath = ClaudeChat::FindCli((ClaudeChat::Backend)gChatBackend);
      gChatClaudeChecked = true;
      if (gChatClaudePath.empty() || !ClaudeChat::LooksLoggedIn((ClaudeChat::Backend)gChatBackend))
         gChatShowSetup = true;
   }

   const ImGuiViewport* vp = ImGui::GetMainViewport();
   ImGui::SetNextWindowSize(ImVec2(460, 640), ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 480, vp->WorkPos.y + 60), ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowSizeConstraints(ImVec2(320, 360), ImVec2(FLT_MAX, FLT_MAX));
   const std::string title = std::string(ChatBackendLabel(gChatBackend)) + "###turbochat";
   if (!ImGui::Begin(title.c_str(), &gChatOpen, ImGuiWindowFlags_NoCollapse))
   {
      ImGui::End();
      return;
   }
   {
      const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
      gChatRect = ImVec4(p.x, p.y, p.x + s.x, p.y + s.y);
   }

   const bool busy = ClaudeChat::Busy();
   const ImGuiStyle& style = ImGui::GetStyle();

   // ---- header: backend switch, model, new, setup, status --------------------
   ImGui::BeginDisabled(busy);
   for (int b = 0; b < 2; b++)
   {
      if (b > 0)
         ImGui::SameLine(0.0f, 2.0f);
      const bool on = gChatBackend == b;
      if (on)
         ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
      if (ImGui::Button(b == 0 ? "Claude" : "ChatGPT"))
         ChatSetBackend(b);
      if (on)
         ImGui::PopStyleColor();
   }
   ImGui::EndDisabled();
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("ChatGPT runs through OpenAI's Codex CLI with your ChatGPT login");
   ImGui::SameLine();
   if (ChatModelNames().size() > 1)
   {
      ImGui::SetNextItemWidth(118.0f);
      if (ImGui::BeginCombo("##chatmodel", ChatModelNames()[std::clamp(gChatModel, 0, (int)ChatModelNames().size() - 1)].c_str()))
      {
         for (int i = 0; i < (int)ChatModelNames().size(); i++)
            if (ImGui::Selectable(ChatModelNames()[i].c_str(), i == gChatModel))
               gChatModel = i;
         ImGui::EndCombo();
      }
      ImGui::SameLine();
   }
   ImGui::BeginDisabled(busy);
   if (ImGui::Button("New"))
   {
      ClaudeChat::Discard(); // a late session id must not bring the old chat back
      gChatLog.clear();
      gChatSession.clear();
   }
   ImGui::EndDisabled();
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("new conversation (the assistant forgets this one; the patch stays)");
   ImGui::SameLine();
   if (gChatShowSetup)
      ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
   const bool setupWas = gChatShowSetup;
   if (ImGui::Button(" ? "))
      gChatShowSetup = !gChatShowSetup;
   if (setupWas)
      ImGui::PopStyleColor();
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("setup: install and log in");
   ImGui::SameLine();
   {
      const bool was = gChatSelectMode;
      if (was)
         ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
      if (ImGui::Button("Select"))
      {
         gChatSelectMode = !gChatSelectMode;
         gChatSelectStamp = 0;
      }
      if (was)
         ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("show the conversation as text you can select (drag, Ctrl+A, Ctrl+C)");
   }
   ImGui::SameLine();
   if (ImGui::Button("Copy all"))
      ImGui::SetClipboardText(ChatTranscript().c_str());
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("copy the whole conversation");
   {
      char status[96];
      if (busy)
      {
         static const char* kDots[] = { ".", "..", "..." };
         snprintf(status, sizeof(status), "working%s %.0fs", kDots[(int)(glfwGetTime() * 3.0) % 3], glfwGetTime() - gChatStarted);
      }
      else
         snprintf(status, sizeof(status), "%s", gChatSession.empty() ? "new conversation" : "conversation open");
      const float w = ImGui::CalcTextSize(status).x;
      ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w));
      ImGui::TextDisabled("%s", status);
   }
   ImGui::Separator();

   if (gChatShowSetup || gChatClaudePath.empty())
   {
      if (ImGui::BeginChild("##chatsetup", ImVec2(0, 0), false))
         DrawChatSetup();
      ImGui::EndChild();
      ImGui::End();
      return;
   }

   // ---- composer size (it grows with its text, 2..8 lines) ------------------
   int lines = 1;
   for (char c : gChatInput)
      lines += c == '\n';
   lines = std::clamp(lines + 1, 2, 8);
   const float lineH = ImGui::GetTextLineHeight();
   const float inputH = lineH * (float)lines + style.FramePadding.y * 2.0f;
   const float chipsH = gChatAttachments.empty() ? 0.0f : ImGui::GetFrameHeight() * 1.6f + style.ItemSpacing.y;
   const float noticeH = gChatNotice.empty() ? 0.0f : lineH + style.ItemSpacing.y;
   const float footer = chipsH + inputH + ImGui::GetFrameHeightWithSpacing() + noticeH + style.ItemSpacing.y * 2.0f;

   // ---- log -------------------------------------------------------------------
   if (gChatSelectMode)
   {
      // Selectable text: a read-only field, rebuilt when the log or the
      // window width changes (wrapped by hand, see ChatWrapForField).
      const float wrapW = ImGui::GetContentRegionAvail().x - style.FramePadding.x * 2.0f - style.ScrollbarSize - 4.0f;
      size_t stamp = gChatLog.size() * 1000003u + (size_t)std::max(0.0f, wrapW);
      if (!gChatLog.empty())
         stamp ^= gChatLog.back().text.size() * 7919u;
      if (stamp != gChatSelectStamp)
      {
         gChatSelectBuf = ChatWrapForField(ChatTranscript(), std::max(50.0f, wrapW));
         gChatSelectStamp = stamp;
      }
      ImGui::InputTextMultiline("##chatselect", &gChatSelectBuf, ImVec2(-1.0f, -footer),
                                ImGuiInputTextFlags_ReadOnly);
   }
   else if (ImGui::BeginChild("##chatlog", ImVec2(0, -footer), false))
   {
      if (gChatLog.empty())
      {
         ImGui::Spacing();
         ImGui::TextDisabled("Ask %s to build or change the patch, e.g.", gChatBackend == ClaudeChat::kCodex ? "ChatGPT" : "Claude");
         ImGui::TextDisabled("  \"make a slow ambient patch with reverb and visuals\"");
         ImGui::TextDisabled("  \"follow the look of the attached image\"");
         ImGui::TextDisabled("  \"what does this patch do?\"");
         ImGui::Spacing();
         ImGui::TextDisabled("Attach files with +, paste a screenshot with Ctrl+V,");
         ImGui::TextDisabled("or drop files on this window.");
      }
      const bool light = IsThemeLight();
      const ImU32 userBg = light ? IM_COL32(205, 222, 250, 255) : IM_COL32(38, 58, 92, 255);
      const ImU32 botBg = light ? IM_COL32(236, 236, 240, 255) : IM_COL32(36, 38, 46, 255);
      const float fullW = ImGui::GetContentRegionAvail().x;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      for (size_t i = 0; i < gChatLog.size(); i++)
      {
         const ChatEntry& e = gChatLog[i];
         ImGui::PushID((int)i);
         if (e.role == ChatEntry::kTool)
         {
            // Fold the run of tool calls into one line that opens on click.
            size_t j = i;
            while (j + 1 < gChatLog.size() && gChatLog[j + 1].role == ChatEntry::kTool)
               j++;
            const int count = (int)(j - i + 1);
            char label[64];
            snprintf(label, sizeof(label), "%d action%s on the patch", count, count == 1 ? "" : "s");
            ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
            if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth))
            {
               for (size_t k = i; k <= j; k++)
               {
                  ImGui::PushTextWrapPos(0.0f);
                  ImGui::TextUnformatted(gChatLog[k].text.c_str());
                  ImGui::PopTextWrapPos();
               }
               ImGui::TreePop();
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
            i = j;
            continue;
         }
         if (e.role == ChatEntry::kInfo)
         {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", e.text.c_str());
            if (ImGui::BeginPopupContextItem("##chatcopyline"))
            {
               if (ImGui::MenuItem("Copy"))
                  ImGui::SetClipboardText(e.text.c_str());
               ImGui::EndPopup();
            }
            ImGui::PopTextWrapPos();
            ImGui::PopID();
            continue;
         }
         if (e.role == ChatEntry::kError)
         {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", e.text.c_str());
            if (ImGui::BeginPopupContextItem("##chatcopyline"))
            {
               if (ImGui::MenuItem("Copy"))
                  ImGui::SetClipboardText(e.text.c_str());
               ImGui::EndPopup();
            }
            ImGui::PopTextWrapPos();
            ImGui::PopID();
            continue;
         }
         // A bubble: user on the right, assistant on the left.
         const bool user = e.role == ChatEntry::kUser;
         const float pad = 8.0f;
         const float bubbleW = fullW * 0.86f;
         ImGui::Dummy(ImVec2(0, 2));
         const float x0 = ImGui::GetCursorPosX() + (user ? fullW - bubbleW : 0.0f);
         ImGui::SetCursorPosX(x0 + pad);
         dl->ChannelsSplit(2);
         dl->ChannelsSetCurrent(1);
         ImGui::BeginGroup();
         ImGui::Dummy(ImVec2(0, pad * 0.5f));
         ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bubbleW - pad * 2.0f);
         ImGui::TextUnformatted(e.text.c_str());
         ImGui::PopTextWrapPos();
         if (!e.attachments.empty())
            DrawChatAttachmentChips(e.attachments, false);
         ImGui::Dummy(ImVec2(0, pad * 0.5f));
         ImGui::EndGroup();
         const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
         dl->ChannelsSetCurrent(0);
         const float left = ImGui::GetWindowPos().x - ImGui::GetScrollX() + x0;
         dl->AddRectFilled(ImVec2(left, rmin.y), ImVec2(std::max(rmax.x + pad, left + 40.0f), rmax.y), user ? userBg : botBg, 8.0f);
         dl->ChannelsMerge();
         if (ImGui::BeginPopupContextItem("##chatcopy"))
         {
            if (ImGui::MenuItem("Copy"))
               ImGui::SetClipboardText(e.text.c_str());
            ImGui::EndPopup();
         }
         ImGui::PopID();
      }
      if (gChatScroll)
      {
         ImGui::SetScrollHereY(1.0f);
         gChatScroll = false;
      }
   }
   if (!gChatSelectMode)
      ImGui::EndChild();

   // ---- composer ----------------------------------------------------------------
   if (!gChatAttachments.empty())
      DrawChatAttachmentChips(gChatAttachments, true);
   if (gChatFocusInput)
   {
      ImGui::SetKeyboardFocusHere();
      gChatFocusInput = false;
   }
   const ImVec2 inputPos = ImGui::GetCursorScreenPos();
   ImGui::InputTextMultiline("##chatinput", &gChatInput, ImVec2(-1.0f, inputH));
   const bool inputActive = ImGui::IsItemActive();
   if (gChatInput.empty() && !inputActive)
      ImGui::GetWindowDrawList()->AddText(ImVec2(inputPos.x + style.FramePadding.x, inputPos.y + style.FramePadding.y),
                                          ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                          "Message  (Enter sends, Shift+Enter new line)");
   const ImGuiIO& chatIo = ImGui::GetIO();
   // Ctrl+V with an image (and no text) on the clipboard attaches it.
   if (inputActive && chatIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
   {
      const char* clip = ImGui::GetClipboardText();
      if (clip == nullptr || clip[0] == '\0')
         ChatPasteImage();
   }
   const bool enter = inputActive && !chatIo.KeyShift && !chatIo.KeyCtrl &&
                      (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
   if (enter && !busy)
   {
      ChatSend();
      gChatFocusInput = true;
   }

   if (ImGui::Button("+ file"))
   {
      const std::string path = Platform::OpenImageDialog(); // its filter list ends with All files
      if (!path.empty())
         ChatAddAttachment(path);
   }
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("attach an image (a reference to follow), a patch or a text file");
   ImGui::SameLine();
   if (ImGui::Button("paste image"))
      ChatPasteImage();
   if (ImGui::IsItemHovered())
      ImGui::SetTooltip("attach the image on the clipboard (Print Screen, Win+Shift+S, a copied picture)");
   ImGui::SameLine();
   const float sendW = 96.0f;
   ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - sendW));
   if (busy)
   {
      if (ImGui::Button("Stop", ImVec2(sendW, 0)))
      {
         ClaudeChat::Cancel();
         ChatAppend(ChatEntry::kInfo, "stopped");
      }
   }
   else
   {
      ImGui::BeginDisabled(gChatInput.find_first_not_of(" \t\r\n") == std::string::npos && gChatAttachments.empty());
      if (ImGui::Button("Send", ImVec2(sendW, 0)))
      {
         ChatSend();
         gChatFocusInput = true;
      }
      ImGui::EndDisabled();
   }
   if (!gChatNotice.empty())
      ImGui::TextDisabled("%s", gChatNotice.c_str());
   ImGui::End();
}

// ---------------------------------------------------------------------------
// Turbo 0.59: Setup > AI connections. Every way an assistant can drive this
// app, in one window: the chat in here, ChatGPT on the web (through a public
// HTTPS tunnel to the MCP server), Claude Desktop, Claude Code, the Codex CLI
// and any other MCP client.
bool gAiConnectionsOpen = false;

void AiCopyRow(const char* id, const std::string& text)
{
   ImGui::SetNextItemWidth(-60.0f);
   ImGui::InputText(id, (char*)text.c_str(), text.size() + 1, ImGuiInputTextFlags_ReadOnly);
   ImGui::SameLine();
   if (ImGui::Button((std::string("Copy") + id).c_str()))
      ImGui::SetClipboardText(text.c_str());
}

void DrawAiConnections()
{
   if (!gAiConnectionsOpen)
      return;
   ImGui::SetNextWindowSize(ImVec2(620, 640), ImGuiCond_FirstUseEver);
   if (!ImGui::Begin("AI connections###aiconnections", &gAiConnectionsOpen, ImGuiWindowFlags_NoCollapse))
   {
      ImGui::End();
      return;
   }
   const std::string exe = Platform::ExecutablePath();
   ImGui::TextWrapped("Infinite-Turbo is an MCP server: any assistant that speaks MCP can read and build the open "
                      "patch with the same tools. Pick how you want to talk to it.");

   if (ImGui::CollapsingHeader("Chat inside Infinite (Claude or ChatGPT)", ImGuiTreeNodeFlags_DefaultOpen))
   {
      ImGui::TextWrapped("Uses your own Claude or ChatGPT login through their command-line apps (Claude Code / "
                         "Codex). Images and files can be attached.");
      if (ImGui::Button("Open the chat (Shift+C)"))
      {
         gChatOpen = true;
         gChatFocusInput = true;
      }
   }

   if (ImGui::CollapsingHeader("ChatGPT on the web (and your GPTs / apps)", ImGuiTreeNodeFlags_DefaultOpen))
   {
      ImGui::TextWrapped("ChatGPT only reaches MCP servers on a public https address. This starts the server over "
                         "HTTP on this PC and a free Cloudflare tunnel to it (no account needed). The address has a "
                         "secret part: only paste it into your own ChatGPT.");
      const McpShare::State st = McpShare::Current();
      const bool running = st != McpShare::kOff && st != McpShare::kFailed;
      if (!running)
      {
         if (ImGui::Button("Start sharing"))
         {
            std::string err;
            if (!McpShare::Start(err))
               gPatchStatus = err;
         }
      }
      else if (ImGui::Button("Stop sharing"))
         McpShare::Stop();
      ImGui::SameLine();
      const char* stText = st == McpShare::kOff ? "off" : st == McpShare::kStarting ? "starting the tunnel..."
                         : st == McpShare::kLocalOnly ? "running on this PC only" : st == McpShare::kPublic ? "online" : "failed";
      ImGui::TextDisabled("status: %s", stText);
      const std::string err = McpShare::LastError();
      if (!err.empty() && st != McpShare::kPublic)
         ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%s", err.c_str());
      if (McpShare::FindCloudflared().empty())
      {
         ImGui::TextWrapped("cloudflared (the tunnel) is not installed:");
         if (ImGui::Button("Install cloudflared (opens a terminal)"))
            ClaudeChat::OpenTerminal("winget install --id Cloudflare.cloudflared -e");
         ImGui::SameLine();
         ImGui::TextDisabled("then Stop / Start sharing");
      }
      const std::string pub = McpShare::PublicUrl();
      if (!pub.empty())
      {
         ImGui::Text("Connector URL:");
         AiCopyRow("##mcppublic", pub);
      }
      if (running)
      {
         ImGui::TextDisabled("Local address (for clients on this PC):");
         AiCopyRow("##mcplocal", McpShare::LocalUrl());
      }
      ImGui::TextWrapped("In ChatGPT: Settings > Apps & Connectors > Advanced > Developer mode on, then Create "
                         "(new connector): name Infinite-Turbo, the Connector URL above, Authentication: No "
                         "authentication. In a chat (or with your Infinite-Turbo specialist GPT) turn the connector on "
                         "from the + menu. Developer mode needs a Plus, Pro, Business, Enterprise or Edu plan.");
      ImGui::TextDisabled("A quick tunnel gets a new address each time it starts: update the connector then.");
      if (ImGui::SmallButton("New secret (old URLs stop working)"))
      {
         McpShare::NewToken();
         if (running)
         {
            std::string e2;
            McpShare::Start(e2);
         }
      }
   }

   if (ImGui::CollapsingHeader("Claude Desktop"))
   {
      if (ImGui::Button("Connect to Claude Desktop"))
         InstallMcpConfig(); // reports in a message box
      ImGui::SameLine();
      ImGui::TextDisabled("then quit and reopen Claude Desktop");
   }

   if (ImGui::CollapsingHeader("Claude Code (terminal)"))
   {
      const std::string cmd = "claude mcp add infinite-turbo -- \"" + exe + "\" --mcp";
      AiCopyRow("##claudecodecmd", cmd);
      if (ImGui::Button("Run it in a terminal##cc"))
         ClaudeChat::OpenTerminal(cmd);
      ImGui::SameLine();
      if (ImGui::Button("Install the AI skill"))
         gPatchStatus = InstallPatchSkill(std::string());
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Writes the patch-building guide to %%USERPROFILE%%\\.claude\\skills\\infinite-turbo-patching");
   }

   if (ImGui::CollapsingHeader("Codex CLI (ChatGPT in the terminal)"))
   {
      const std::string cmd = "codex mcp add infinite-turbo -- \"" + exe + "\" --mcp";
      AiCopyRow("##codexcmd", cmd);
      if (ImGui::Button("Run it in a terminal##cx"))
         ClaudeChat::OpenTerminal(cmd);
   }

   if (ImGui::CollapsingHeader("Other MCP clients (Cursor, VS Code, Windsurf, LM Studio...)"))
   {
      std::string escaped;
      for (char c : exe)
      {
         if (c == '\\' || c == '"')
            escaped.push_back('\\');
         escaped.push_back(c);
      }
      const std::string snippet = "{ \"mcpServers\": { \"infinite-turbo\": { \"command\": \"" + escaped +
                                  "\", \"args\": [\"--mcp\"] } } }";
      ImGui::TextWrapped("Add this to the client's MCP settings (stdio). Clients that only take a URL can use the "
                         "local or public address from the ChatGPT section.");
      AiCopyRow("##mcpjson", snippet);
   }

   if (ImGui::CollapsingHeader("The patch-building guide (skill)"))
   {
      ImGui::TextWrapped("Over MCP the guide is the authoring_guide tool. To hand it to an assistant as a file "
                         "(a Claude skill, a GPT's knowledge), save it:");
      if (ImGui::Button("Save AI skill to a folder..."))
      {
         const std::string folder = Platform::OpenFolderDialog("Folder for the infinite-turbo-patching skill");
         if (!folder.empty())
            gPatchStatus = InstallPatchSkill(folder);
      }
   }
   ImGui::End();
}
