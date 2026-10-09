// Templates: small starter patches shipped in Resources/templates (written by tools/templates/gen_templates.py), with
// a thumbnail each. Opening one makes an untitled copy: the shipped file is never the open document, so Save asks
// where to put it and the original can't be overwritten. Also draws the empty-canvas invitation.
#include "app/AppShared.h"
#include "app/ui/design/components/ActionButton.h"
#include "app/ui/design/components/PanelFrame.h"
#include <cstdio>
#include <fstream>
#include <sstream>

namespace app
{
namespace
{
struct Entry
{
   std::string slug, group, title, blurb;
   unsigned int tex = 0;
   bool texTried = false;
};

std::vector<Entry>& Entries()
{
   static std::vector<Entry> sEntries;
   static bool sRead = false;
   if (!sRead)
   {
      sRead = true;
      std::ifstream in(BundledResourcePath("templates/index.txt"));
      std::string line;
      while (std::getline(in, line))
      {
         if (line.empty() || line[0] == '#')
            continue;
         std::vector<std::string> f;
         std::stringstream ss(line);
         std::string part;
         while (std::getline(ss, part, '|'))
            f.push_back(part);
         if (f.size() >= 4)
            sEntries.push_back({ f[0], f[1], f[2], f[3] });
      }
   }
   return sEntries;
}

ImTextureID Thumb(Entry& e)
{
   if (!e.texTried)
   {
      e.texTried = true;
      const std::string path = BundledResourcePath(("templates/thumbs/" + e.slug + ".png").c_str());
      int w = 0, h = 0, n = 0;
      std::ifstream in(path, std::ios::binary);
      const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      unsigned char* px = bytes.empty() ? nullptr
                                        : stbi_load_from_memory((const unsigned char*)bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
      if (px != nullptr)
      {
         GLuint t = 0;
         glGenTextures(1, &t);
         glBindTexture(GL_TEXTURE_2D, t);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
         stbi_image_free(px);
         e.tex = t;
      }
   }
   return (ImTextureID)(intptr_t)e.tex;
}

bool sOpen = false;

void OpenEntry(const Entry& e)
{
   const std::string path = BundledResourcePath(("templates/" + e.slug + ".inf").c_str());
   GuardUnsavedChanges([path]() { OpenTemplate(path); });
   sOpen = false;
}

// One card: thumbnail, title, one line. Drawn on a fixed rectangle; returns true when clicked.
bool Card(Entry& e, ImVec2 pos, float w)
{
   const bool light = CategoryColors::IsThemeLight();
   ImDrawList* dl = ImGui::GetWindowDrawList();
   const float thumb = w - 2.0f * tok::space_2;
   const float h = thumb + ImGui::GetTextLineHeight() * 3.2f + 2.0f * tok::space_2;
   ImGui::SetCursorScreenPos(pos);
   ImGui::PushID(e.slug.c_str());
   const bool clicked = ImGui::InvisibleButton("##card", ImVec2(w, h));
   const bool hovered = ImGui::IsItemHovered();
   ImGui::PopID();
   const ImVec2 a = pos, b(pos.x + w, pos.y + h);
   dl->AddRectFilled(a, b, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), tok::radius_tile);
   const ImVec2 ta(a.x + tok::space_2, a.y + tok::space_2), tb(ta.x + thumb, ta.y + thumb);
   if (ImTextureID t = Thumb(e))
      dl->AddImageRounded(t, ta, tb, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, tok::radius_field);
   else
      dl->AddRectFilled(ta, tb, ImGui::GetColorU32(ImGuiCol_Border), tok::radius_field);
   (void)light;
   const float ty = tb.y + tok::space_2;
   dl->AddText(ImVec2(ta.x, ty), ImGui::GetColorU32(ImGuiCol_Text), e.title.c_str());
   dl->PushClipRect(ImVec2(ta.x, ty), ImVec2(tb.x, b.y), true);
   dl->AddText(ImVec2(ta.x, ty + ImGui::GetTextLineHeight() * 1.2f), ImGui::GetColorU32(ImGuiCol_TextDisabled), e.blurb.c_str());
   dl->PopClipRect();
   if (hovered)
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
   return clicked;
}
}

void OpenTemplatesWindow() { sOpen = true; }

void DrawTemplates()
{
   std::vector<Entry>& entries = Entries();
   if (entries.empty())
      return;

   const ImGuiViewport* vp = ImGui::GetMainViewport();
   static const bool sOpenForReview = getenv("INFINITE_OPENTEMPLATES") != nullptr;   // screenshot hook
   if (sOpenForReview && ImGui::GetFrameCount() == 5)
      sOpen = true;

   // Empty canvas: one quiet invitation in the middle, gone as soon as there is a node.
   if (gNodes.empty() && !sOpen && !HeadlessJobActive())
   {
      ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                              ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      PanelFrame::PushFloatingStyle();
      ImGui::Begin("##templatesInvite", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
      ImGui::TextUnformatted(T("Nothing here yet."));
      ImGui::TextDisabled("%s", T("Start from a small patch that already works, or add a node from the library."));
      ImGui::Dummy(ImVec2(0.0f, tok::space_2));
      if (ActionButton::Draw(T("Start from a template"), ImVec2(0, 0), ActionButton::Kind::Primary))
         sOpen = true;
      ImGui::End();
      PanelFrame::PopFloatingStyle();
   }

   if (!sOpen)
      return;

   const float cardW = ImGui::GetFontSize() * 14.0f;
   const int cols = 4;
   const float pad = tok::space_4;
   const float winW = cols * cardW + (cols - 1) * tok::space_2 + 2.0f * pad;
   ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                           ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
   ImGui::SetNextWindowSize(ImVec2(winW, std::min(vp->WorkSize.y * 0.85f, ImGui::GetFontSize() * 44.0f)), ImGuiCond_Appearing);
   PanelFrame::PushFloatingStyle();
   bool open = true;
   if (ImGui::Begin(T("Start from a template"), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
   {
      ImGui::TextDisabled("%s", T("Each one opens as an untitled copy, so the original is never changed."));
      const char* lastGroup = "";
      int col = 0;
      float rowTop = 0.0f;
      float cardH = 0.0f;
      for (Entry& e : entries)
      {
         if (e.group != lastGroup)
         {
            lastGroup = e.group.c_str();
            if (col != 0)
               ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + pad, rowTop + cardH + tok::space_2));
            col = 0;
            ImGui::Dummy(ImVec2(0.0f, tok::space_2));
            ImGui::TextUnformatted(T(e.group.c_str()));
            rowTop = ImGui::GetCursorScreenPos().y + tok::space_1;
         }
         const float x0 = ImGui::GetWindowPos().x + pad - ImGui::GetScrollX();
         const ImVec2 pos(x0 + col * (cardW + tok::space_2), rowTop);
         cardH = cardW - 2.0f * tok::space_2 + ImGui::GetTextLineHeight() * 3.2f + 2.0f * tok::space_2;
         if (Card(e, pos, cardW))
            OpenEntry(e);
         if (++col == cols)
         {
            col = 0;
            rowTop += cardH + tok::space_2;
         }
      }
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + pad, rowTop + (col != 0 ? cardH : 0.0f)));
      ImGui::Dummy(ImVec2(0.0f, tok::space_2));
   }
   ImGui::End();
   PanelFrame::PopFloatingStyle();
   if (!open || ImGui::IsKeyPressed(ImGuiKey_Escape))
      sOpen = false;
}
}
