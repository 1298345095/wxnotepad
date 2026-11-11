#include "native_search_helpers.h"
#include <wx/vlbox.h>
#include "completion_reference.h"
#ifdef __WXGTK__
#include <gdk/gdkkeysyms.h>
#endif
namespace {
using Scintilla::Message;
void Require(bool value, const char* message) { if (!value) throw TestEditor::Failure(message); }
void NativeKey(EditorControl* editor,int key) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, key, 0);
#elif defined(__WXGTK__)
    auto* event=gdk_event_new(GDK_KEY_PRESS);
    event->key.window=static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->key.send_event=TRUE; event->key.keyval=key;
    gtk_widget_event(editor->GetConnectWidget(),event); gdk_event_free(event);
#endif
    wxYield();
}
std::vector<std::string> Candidates(EditorControl* editor) {
    std::vector<std::string> words;
    intptr_t previous=-1;
    while(editor->Send(Message::AutoCActive)) {
        const auto current=editor->Send(Message::AutoCGetCurrent);
        if(current==previous) break;
        previous=current;
        const auto size=editor->Send(Message::AutoCGetCurrentText);
        std::string word(size+1,0);
        editor->Send(Message::AutoCGetCurrentText,0,reinterpret_cast<intptr_t>(word.data()));
        word.pop_back(); words.push_back(word);
#ifdef __WXMSW__
        NativeKey(editor,VK_DOWN);
#else
        NativeKey(editor,GDK_KEY_Down);
#endif
    }
    return words;
}
class WordCompletionApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "alpha al", 8, 8);
                const auto id = frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word");
                Require(id != wxNOT_FOUND, "Edit > Special > Complete Word exists");
                NativeSearch::Command(frame, id);
                Require(TestEditor::Text(editor) == "alpha alpha", "manual completion inserts original sole candidate");
                TestEditor::RequireSelection(editor, 11, 11, "single candidate caret");
                Require(!editor->Send(Message::AutoCActive), "single candidate has no popup");
                editor->Send(Message::Undo);
                Require(TestEditor::Text(editor) == "alpha al" && !editor->Send(Message::CanUndo), "one Undo restores completion");
                std::puts("PASS: original Complete Word single-candidate command and Undo");
                for(const auto& fixture:completionReference) {
                    std::fprintf(stderr,"Checking %s",fixture.language);
                    editor->Send(Message::AutoCCancel);
                    NativeSearch::Command(frame,frame->GetMenuBar()->FindMenuItem("Scheme",std::string_view(fixture.language)=="HTML" ? "Web Source Code" : std::string_view(fixture.language)=="XML" ? "XML Document" : fixture.language));
                    const auto length=std::strlen(fixture.before);
                    TestEditor::ResetStream(editor,fixture.before,length,length);
                    editor->Send(Message::Colourise,0,-1);
                    NativeSearch::Command(frame,id);
                    const auto actual=Candidates(editor);
                    if(TestEditor::Text(editor)!=fixture.after || actual!=fixture.items) {
                        std::fprintf(stderr,"Fixture %s: %s\nActual text: %s\n",fixture.language,fixture.before,TestEditor::Text(editor).c_str());
                        for(const auto& word:actual) std::fprintf(stderr,"[%s] ",word.c_str());
                        std::fprintf(stderr,"\n");
                    }
                    Require(TestEditor::Text(editor)==fixture.after,"original command bytes");
                    Require(actual==fixture.items,"original candidate list and native Down order");
                    TestEditor::RequireSelection(editor,fixture.caret,fixture.caret,"original command caret");
                }
                std::puts("PASS: frozen original candidate lists, case, numbers, CJK, keywords and prefix fallbacks");
                editor->Send(Message::AutoCCancel);
                NativeSearch::Command(frame,frame->GetMenuBar()->FindMenuItem("Scheme","C/C++"));
                TestEditor::ResetStream(editor,"pri",3,3);
                NativeSearch::Command(frame,id);
#ifdef __WXMSW__
                NativeKey(editor,VK_RETURN);
#else
                NativeKey(editor,GDK_KEY_Return);
#endif
                Require(TestEditor::Text(editor)=="printf()","native Enter confirms original function candidate");
                TestEditor::RequireSelection(editor,7,7,"native confirmation leaves caret inside parentheses");
                editor->Send(Message::Undo);
                Require(TestEditor::Text(editor)=="pri" && !editor->Send(Message::CanUndo),"confirmation is one Undo step");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what()); result_ = 1;
            }
            frame->Destroy(); ExitMainLoop();
        }); return true;
    }
    int OnRun() override { wxApp::OnRun(); return result_; }
private: int result_ = 0;
};
}
wxIMPLEMENT_APP(WordCompletionApp);
