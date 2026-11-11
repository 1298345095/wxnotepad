#include "native_search_helpers.h"
#include "settings/settings_store.h"
namespace {
void Require(bool value,const char* message) { if(!value) throw TestEditor::Failure(message); }
class CompletionSettingsApp final : public wxApp {
public:
 bool OnInit() override {
    CallAfter([this] {
      EditorFrame* frame=nullptr;
      try {
        SettingsStore store;
        frame=new EditorFrame(&store); frame->ShowWithoutActivating();
        const auto& settings=store.values.completion;
        const wxString mode=argc>1 ? argv[1] : wxString("defaults");
        if(mode=="custom") {
          Require(settings.ignoreCase && settings.latexInputMethod,"load original booleans");
          Require(settings.completeOption==12 && settings.completeScope==3 && settings.scanWordScope==5,"load option/scope bitfields");
          Require(settings.scanWordsTimeout==75 && settings.visibleItemCount==24 && settings.minWordLength==2 && settings.minNumberLength==4,"load numeric original preferences");
          Require(settings.fillUpMask==5 && settings.fillUpPunctuation=="!?:","load punctuation and fill-up mask");
        } else if(mode=="bounds") {
          Require(settings.scanWordsTimeout==50 && settings.visibleItemCount==8 && settings.minWordLength==1 && settings.minNumberLength==0,"original lower bounds");
        } else {
          Require(!settings.ignoreCase && !settings.latexInputMethod && settings.completeOption==7,"original completion defaults");
          Require(settings.completeScope==15 && settings.scanWordScope==15 && settings.scanWordsTimeout==500 && settings.visibleItemCount==16 && settings.minWordLength==1 && settings.minNumberLength==3,"original numeric defaults");
          Require(settings.fillUpMask==15 && settings.fillUpPunctuation==WordCompletionSettings{}.fillUpPunctuation,"default punctuation");
        }
        auto* editor=frame->Editor();
        TestEditor::ResetStream(editor,"al",2,2);
        editor->Send(Scintilla::Message::AutoCShow,2,reinterpret_cast<intptr_t>("alpha alpine"));
        const auto id=frame->GetMenuBar()->FindMenuItem("Settings","Auto Completion Ignore Case");
        Require(id!=wxNOT_FOUND,"original ignore-case menu exists");
        const bool before=settings.ignoreCase;
        NativeSearch::Command(frame,id);
        Require(settings.ignoreCase!=before && !editor->Send(Scintilla::Message::AutoCActive),"ignore-case toggle cancels live popup");
        const bool explicitSave=argc>2 && argv[2]=="explicit";
        Require(store.Save(explicitSave),"completion preferences save");
        std::puts("PASS: original completion settings load, toggle and save");
      } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); result_=1; }
      if(frame) frame->Destroy(); ExitMainLoop();
    }); return true;
 }
 int OnRun() override { wxApp::OnRun();return result_; }
private:int result_=0;
};
}
wxIMPLEMENT_APP(CompletionSettingsApp);
