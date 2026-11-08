#include "tab_settings_helpers.h"
#include "file_helpers.h"
#include <filesystem>
#include <fstream>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Check(EditorControl* editor, int tab, int indent, bool spaces) {
    std::fprintf(stderr, "Indentation actual=%lld/%lld/%lld expected=%d/%d/%d\n",
                 static_cast<long long>(editor->Send(Message::GetTabWidth)),
                 static_cast<long long>(editor->Send(Message::GetIndent)),
                 static_cast<long long>(editor->Send(Message::GetUseTabs)), tab, indent, !spaces);
    Require(editor->Send(Message::GetTabWidth) == tab && editor->Send(Message::GetIndent) == indent &&
                editor->Send(Message::GetUseTabs) == !spaces,
            "opened indentation must match pinned original policy");
}
class FileIndentationApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-file-indentation");
            try {
                std::filesystem::create_directories(directory);
                const auto path = directory / "spaces.txt";
                const std::string bytes = "root\n  one\n    two\n  three\n";
                {
                    std::ofstream output(path, std::ios::binary);
                    output << bytes;
                }
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "Open succeeds");
                Check(frame->Editor(), 2, 2, true);
                Require(NativeSearch::Text(frame->Editor()) == bytes && !frame->Editor()->Send(Message::GetModify),
                        "detection preserves bytes and save point");
                std::puts("PASS: original space indentation detection through Open");
                const auto variables = directory / "variables.txt";
                {
                    std::ofstream output(variables, std::ios::binary);
                    output << "# tab-width: 7; c-basic-offset: 3; indent-tabs-mode: true\n"
                              "root\n  one\n    two\n  three\n";
                }
                Require(frame->Open(TestFiles::Filename(variables)).status == OpenStatus::Opened,
                        "variable Open succeeds");
                Check(frame->Editor(), 7, 3, false);
                std::puts("PASS: complete original file variables bypass detection");
                TabDialog::Show(frame, [](wxDialog* dialog) {
                    TabDialog::Toggle(TabDialog::Option(dialog, "Detect Indentation"), false);
                });
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                        "disabled detector Open succeeds");
                Check(frame->Editor(), 4, 4, false);
                std::puts("PASS: original detection option applies on the next Open");
                const auto open = [&](const char* name, const std::string& text, int tab, int indent, bool spaces) {
                    const auto candidate = directory / name;
                    {
                        std::ofstream output(candidate, std::ios::binary);
                        output << text;
                    }
                    Require(frame->Open(TestFiles::Filename(candidate)).status == OpenStatus::Opened, name);
                    Check(frame->Editor(), tab, indent, spaces);
                    Require(NativeSearch::Text(frame->Editor()) == text && !frame->Editor()->Send(Message::GetModify) &&
                                !frame->Editor()->Send(Message::CanUndo),
                            "Open applies indentation without changing loaded bytes or history");
                };
                open("width-only.txt", "# tab-width: 7\nplain\n", 7, 7, false);
                open("indent-only.txt", "# c-basic-offset: 3\nplain\n", 3, 3, false);
                open("clamped.txt", "# tab-width: 999; c-basic-indent: -5; indent-tabs-mode: nil\n", 256, 0, true);
                open("grammar.txt", "# atab-width: 9; tab-width = '6'; x-basic-indent: 2; indent-tabs-mode = false\n",
                     6, 2, true);
                open("disabled.txt", "# enable-local-variables: no; tab-width: 9; indent-tabs-mode: nil\n", 4, 4,
                     false);
                open("case.txt", "# Tab-width: 9; some_tab-width: 8; .tab-width: 7; basic_offset: 6\n", 4, 4, false);
                const std::string tail = "\n# tab-width: 6; indent-tabs-mode: nil\n";
                const std::string padding(600, 'x');
                open("tail.txt", padding + tail, 6, 6, true);
                open("head-wins.txt", "# mode: text\n" + padding + tail, 4, 4, false);
                open("empty-mode-gate.txt", "# mode: ;\n" + padding + tail, 4, 4, false);
                open("encoding-gate.txt", "# coding: utf-8\n" + padding + tail, 4, 4, false);
                // UTF-8 BOM suppresses encoding tags in the beginning window, allowing the tail.
                const auto bomPath = directory / "bom.txt";
                {
                    std::ofstream output(bomPath, std::ios::binary);
                    output << "\xef\xbb\xbf# coding: utf-8\n" << padding << tail;
                }
                Require(frame->Open(TestFiles::Filename(bomPath)).status == OpenStatus::Opened, "BOM file opens");
                Check(frame->Editor(), 6, 6, true);
                open("tab-indent.txt", "# c-tab-always-indent: false; tab-width: 7; indent-tabs-mode: true\n", 7, 7,
                     false);
                Require(!frame->Editor()->Send(Message::GetTabIndents), "file Tab Indents variable applies");
                for (const auto* language : {"JSON", "Python", "C/C++", "Plain Text"}) {
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
                    Check(frame->Editor(), 7, 7, false);
                    Require(!frame->Editor()->Send(Message::GetTabIndents), "language changes retain file Tab Indents");
                }
                Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled &&
                            frame->Open(TestFiles::Filename(directory / "missing.txt")).status == OpenStatus::Failed,
                        "cancelled and failed Open preserve current overrides");
                Check(frame->Editor(), 7, 7, false);
                Require(frame->NewDocument(), "New succeeds");
                Check(frame->Editor(), 4, 4, false);
                Require(frame->Editor()->Send(Message::GetTabIndents), "New restores global Tab Indents");
                TabDialog::Show(frame, [](wxDialog* dialog) {
                    TabDialog::Toggle(TabDialog::Option(dialog, "Detect Indentation"), true);
                });
                open("tabs.txt", "root\n\tone\n\t\ttwo\n", 4, 4, false);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "JSON"));
                Check(frame->Editor(), 2, 2, false);
                open("mixed.txt", " \talpha\n", 1, 1, true);
                open("lower-tie.txt", " a\nroot\n  b\n", 1, 1, true);
                open("tabs-tie.txt", "\talpha\n  beta\n", 2, 2, true);
                open("ambiguous-tie.txt", "         alpha\nroot\n  beta\n", 4, 4, false);
                open("javadoc.txt", " * alpha\n * beta\n", 4, 4, false);
                open("javadoc-indented.txt", "   * alpha\n   * beta\n", 2, 2, true);
                open("partial-width.txt", "# tab-width: 7\nroot\n  alpha\n    beta\n", 2, 2, true);
                open("partial-choice.txt", "# indent-tabs-mode: nil\n\talpha\n\tbeta\n", 4, 4, false);
                open("partial-tabs.txt", "# tab-width: 7\n\talpha\n\tbeta\n", 7, 7, false);
                open("variables-disabled-detection.txt", "# enable-local-variables: false; tab-width: 7\n  alpha\n", 2,
                     2, true);
                open("no-indent.txt", "plain\ntext\n", 4, 4, false);
                open("sample-limit.txt", std::string(1024 * 1024, 'x') + "\n  alpha\n  beta\n", 4, 4, false);
                open("detected.txt", "  alpha\n    beta\n  gamma\n", 2, 2, true);
                for (const auto* language : {"JSON", "Python", "Plain Text"}) {
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
                    Check(frame->Editor(), 2, 2, true);
                }
                Require(frame->SaveAs(TestFiles::Filename(directory / "saved.json"), {}, SaveType::AllFiles).status ==
                            SaveStatus::Saved,
                        "Save As succeeds");
                Check(frame->Editor(), 2, 2, true);
                Require(TestFiles::Bytes(directory / "saved.json") == "  alpha\n    beta\n  gamma\n",
                        "Save preserves whitespace bytes");
                Require(frame->NewDocument(), "New after detection succeeds");
                Check(frame->Editor(), 4, 4, false);
                std::puts("PASS: variable grammar, clamping, beginning/end gates, partial precedence, ties, sampling "
                          "and transitions");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            std::filesystem::remove_all(directory);
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(FileIndentationApp);
