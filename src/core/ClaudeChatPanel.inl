// Infinite-Turbo 0.47: the Claude chat window (CLAUDE in the top bar,
// Shift+C). Runs the user's own Claude Code CLI with this app's MCP server,
// so Claude edits the open patch with the same tools as in Claude Desktop.
// Included by main.cpp inside its anonymous namespace; backend in
// platform/ClaudeChat*.

struct ChatEntry
{
   enum Role { kUser, kAssistant, kTool, kInfo, kError } role = kInfo;
   std::string text;
};

bool gChatOpen = false;
std::vector<ChatEntry> gChatLog;
std::string gChatSession;
char gChatInput[4096] = "";
int gChatModel = 0;
bool gChatScroll = false;
bool gChatFocusInput = false;
double gChatStarted = 0.0;
std::string gChatClaudePath; // cached FindClaude(); re-checked on demand
bool gChatClaudeChecked = false;

const std::vector<std::string>& ChatModelNames()
{
   static const std::vector<std::string> names = { "default model", "sonnet", "opus", "haiku" };
   return names;
}

const char* kChatSystemPrompt =
   "You are running inside Infinite-Turbo, a node-based audiovisual patching app, through its built-in chat "
   "window. The user is looking at the app. Use the infinite-turbo MCP tools to inspect and change the patch "
   "that is open: explain shows the live graph; call authoring_guide once before building something new; "
   "screenshot_node and render_frame let you look at the picture; drum_pattern loads grooves (A verse, "
   "B bridge, C chorus). Do the work with the tools instead of describing it. Reply in the user's language, "
   "briefly, as plain text (no tables, no headings). Every change is undoable with Ctrl+Z in the app.";

void ChatAppend(ChatEntry::Role role, const std::string& text)
{
   // Consecutive assistant blocks read as one message.
   if (role == ChatEntry::kAssistant && !gChatLog.empty() && gChatLog.back().role == ChatEntry::kAssistant)
      gChatLog.back().text += "\n\n" + text;
   else
      gChatLog.push_back({ role, text });
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
         case ClaudeChat::Event::kTool: ChatAppend(ChatEntry::kTool, "> " + e.text); break;
         case ClaudeChat::Event::kToolError: ChatAppend(ChatEntry::kError, "tool error: " + e.text); break;
         case ClaudeChat::Event::kSession: gChatSession = e.text; break;
         case ClaudeChat::Event::kInfo: break; // model line: shown in the status, not the log
         case ClaudeChat::Event::kError:
         {
            std::string text = e.text;
            if (text.find("login") != std::string::npos || text.find("Login") != std::string::npos ||
                text.find("authenticat") != std::string::npos)
               text += "\n\nOpen a Command Prompt, run `claude`, and log in with your Claude account once.";
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

void ChatSend()
{
   std::string prompt = gChatInput;
   while (!prompt.empty() && (prompt.back() == '\n' || prompt.back() == ' '))
      prompt.pop_back();
   if (prompt.empty() || ClaudeChat::Busy())
      return;
   if (gChatClaudePath.empty())
      gChatClaudePath = ClaudeChat::FindClaude();
   ClaudeChat::Options opt;
   opt.claudePath = gChatClaudePath;
   opt.exePath = Platform::ExecutablePath();
   const std::string settingsDir = InfiniteSettingsDirectory();
   opt.workDir = settingsDir.empty() ? std::string(".") : (std::filesystem::u8path(settingsDir) / "chat").u8string();
   opt.sessionId = gChatSession;
   opt.model = gChatModel > 0 ? ChatModelNames()[gChatModel] : std::string();
   opt.systemPrompt = kChatSystemPrompt;
   std::string error;
   ChatAppend(ChatEntry::kUser, prompt);
   if (!ClaudeChat::Send(prompt, opt, error))
   {
      ChatAppend(ChatEntry::kError, error);
      return;
   }
   gChatInput[0] = '\0';
   gChatStarted = glfwGetTime();
}

void DrawClaudeNotInstalled()
{
   ImGui::TextWrapped("The chat uses Claude Code, the command-line Claude, with your own Claude account "
                      "(Pro or Max), so it costs nothing beyond your plan. It is not installed yet.");
   ImGui::Spacing();
   ImGui::TextWrapped("1. Open a Command Prompt (cmd) and paste:");
   static const char* kInstall = "curl -fsSL https://claude.ai/install.cmd -o install.cmd && install.cmd && del install.cmd";
   ImGui::SetNextItemWidth(-1.0f);
   ImGui::InputText("##chatinstall", (char*)kInstall, strlen(kInstall) + 1, ImGuiInputTextFlags_ReadOnly);
   if (ImGui::Button("Copy##chatinstallcopy"))
      ImGui::SetClipboardText(kInstall);
   ImGui::Spacing();
   ImGui::TextWrapped("2. Then run  claude  once in the same window and log in with your Claude account.");
   ImGui::TextWrapped("3. Come back here and press Check again.");
   ImGui::Spacing();
   if (ImGui::Button("Check again"))
   {
      gChatClaudePath = ClaudeChat::FindClaude();
      gChatClaudeChecked = true;
   }
   ImGui::TextDisabled("(or set INFINITE_CLAUDE_PATH to claude.exe)");
}

void DrawClaudeChat()
{
   ChatPump(); // also while closed, so a turn finishing in the background lands in the log
   if (!gChatOpen)
      return;
   if (!gChatClaudeChecked)
   {
      gChatClaudePath = ClaudeChat::FindClaude();
      gChatClaudeChecked = true;
   }

   const ImGuiViewport* vp = ImGui::GetMainViewport();
   ImGui::SetNextWindowSize(ImVec2(440, 600), ImGuiCond_FirstUseEver);
   ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 460, vp->WorkPos.y + 60), ImGuiCond_FirstUseEver);
   if (!ImGui::Begin("Claude##turbochat", &gChatOpen, ImGuiWindowFlags_NoCollapse))
   {
      ImGui::End();
      return;
   }

   if (gChatClaudePath.empty())
   {
      DrawClaudeNotInstalled();
      ImGui::End();
      return;
   }

   const bool busy = ClaudeChat::Busy();
   // Header: model, new conversation, status.
   ImGui::SetNextItemWidth(130.0f);
   if (ImGui::BeginCombo("##chatmodel", ChatModelNames()[gChatModel].c_str()))
   {
      for (int i = 0; i < (int)ChatModelNames().size(); i++)
         if (ImGui::Selectable(ChatModelNames()[i].c_str(), i == gChatModel))
            gChatModel = i;
      ImGui::EndCombo();
   }
   ImGui::SameLine();
   ImGui::BeginDisabled(busy);
   if (ImGui::Button("New chat"))
   {
      ClaudeChat::Discard(); // a late session id must not bring the old chat back
      gChatLog.clear();
      gChatSession.clear();
   }
   ImGui::EndDisabled();
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("start a new conversation (Claude forgets this one; the patch stays)");
   ImGui::SameLine();
   if (busy)
   {
      static const char* kDots[] = { ".", "..", "..." };
      ImGui::TextDisabled("working%s %.0fs", kDots[(int)(glfwGetTime() * 3.0) % 3], glfwGetTime() - gChatStarted);
   }
   else
      ImGui::TextDisabled("%s", gChatSession.empty() ? "new conversation" : "conversation open");
   ImGui::Separator();

   // Log.
   const float inputH = ImGui::GetTextLineHeight() * 3.0f + ImGui::GetStyle().FramePadding.y * 2.0f;
   const float footer = inputH + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
   if (ImGui::BeginChild("##chatlog", ImVec2(0, -footer), false))
   {
      if (gChatLog.empty())
      {
         ImGui::TextDisabled("Ask Claude to build or change the patch, e.g.");
         ImGui::TextDisabled("  \"make a slow ambient patch with reverb and visuals\"");
         ImGui::TextDisabled("  \"load an ijexa groove in the drum sequencer, part C\"");
         ImGui::TextDisabled("  \"what does this patch do?\"");
      }
      const bool light = IsThemeLight();
      for (size_t i = 0; i < gChatLog.size(); i++)
      {
         const ChatEntry& e = gChatLog[i];
         ImGui::PushID((int)i);
         ImGui::PushTextWrapPos(0.0f);
         switch (e.role)
         {
            case ChatEntry::kUser:
               ImGui::Spacing();
               ImGui::TextColored(light ? ImVec4(0.15f, 0.35f, 0.75f, 1.0f) : ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "you");
               ImGui::TextUnformatted(e.text.c_str());
               break;
            case ChatEntry::kAssistant:
               ImGui::Spacing();
               ImGui::TextColored(light ? ImVec4(0.70f, 0.38f, 0.15f, 1.0f) : ImVec4(0.95f, 0.65f, 0.40f, 1.0f), "claude");
               ImGui::TextUnformatted(e.text.c_str());
               break;
            case ChatEntry::kTool:
               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
               ImGui::TextUnformatted(e.text.c_str());
               ImGui::PopStyleColor();
               break;
            case ChatEntry::kInfo:
               ImGui::TextDisabled("%s", e.text.c_str());
               break;
            case ChatEntry::kError:
               ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", e.text.c_str());
               break;
         }
         ImGui::PopTextWrapPos();
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
   ImGui::EndChild();

   // Input: Enter sends, Ctrl+Enter is a new line.
   if (gChatFocusInput)
   {
      ImGui::SetKeyboardFocusHere();
      gChatFocusInput = false;
   }
   const bool enter = ImGui::InputTextMultiline("##chatinput", gChatInput, sizeof(gChatInput), ImVec2(-1.0f, inputH),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
   if (enter && !busy)
   {
      ChatSend();
      gChatFocusInput = true;
   }
   if (busy)
   {
      if (ImGui::Button("Stop", ImVec2(-1.0f, 0)))
      {
         ClaudeChat::Cancel();
         ChatAppend(ChatEntry::kInfo, "stopped");
      }
   }
   else
   {
      ImGui::BeginDisabled(gChatInput[0] == '\0');
      if (ImGui::Button("Send  (Enter)", ImVec2(-1.0f, 0)))
      {
         ChatSend();
         gChatFocusInput = true;
      }
      ImGui::EndDisabled();
   }
   ImGui::End();
}
