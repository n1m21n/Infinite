// Windows and Linux: no editor view until chunk 3 (README 10); hosts show their generic editor.
#include "plugin/shell/ShellView.h"

namespace InfinitePlugin
{
   bool ShellView::Supported() { return false; }
   ShellView* ShellView::Create(ViewHost*) { return nullptr; }
}
