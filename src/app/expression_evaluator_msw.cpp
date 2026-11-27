#include "app/expression_evaluator.h"
#include <windows.h>
#include <activscp.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>

// Adapted from Notepad4 89b8b36 Bridge.cpp::EditCalculateExpr.
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
class ScriptSite final : public IActiveScriptSite {
public:
    ExpressionEvaluation result;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
        *output = nullptr;
        if (id != IID_IUnknown && id != IID_IActiveScriptSite) return E_NOINTERFACE;
        *output = this;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetLCID(LCID* locale) override {
        *locale = LOCALE_USER_DEFAULT;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDocVersionString(BSTR* version) override {
        *version = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetItemInfo(LPCOLESTR, DWORD, IUnknown**, ITypeInfo**) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnScriptTerminate(const VARIANT*, const EXCEPINFO*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStateChange(SCRIPTSTATE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnEnterScript() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnLeaveScript() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnScriptError(IActiveScriptError* error) override {
        EXCEPINFO information{};
        if (SUCCEEDED(error->GetExceptionInfo(&information)) && information.bstrDescription) {
            result.error = information.bstrDescription;
            ULONG line = 0;
            LONG column = 0;
            error->GetSourcePosition(nullptr, &line, &column);
            result.line = line > 1 ? line - 1 : 0;
            result.column = static_cast<size_t>(std::max<LONG>(0, column));
        }
        SysFreeString(information.bstrDescription);
        SysFreeString(information.bstrSource);
        SysFreeString(information.bstrHelpFile);
        return S_OK;
    }
};
} // namespace

ExpressionEvaluation EvaluateExpression(const wxString& expression, size_t resultCharacters) {
    using Microsoft::WRL::ComPtr;
    constexpr CLSID Chakra{0x1b7cd997, 0xe5ff, 0x4932, {0xa7, 0xa6, 0x2a, 0x9e, 0x63, 0x6d, 0xa3, 0x85}};
    ComPtr<IActiveScript> engine;
    if (FAILED(CoCreateInstance(Chakra, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&engine)))) {
        CLSID javaScript{};
        if (FAILED(CLSIDFromProgID(L"JavaScript", &javaScript)) ||
            FAILED(CoCreateInstance(javaScript, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&engine))))
            return {};
    }
    ScriptSite site;
    ComPtr<IActiveScriptParse> parser;
    if (SUCCEEDED(engine->SetScriptSite(&site)) && SUCCEEDED(engine.As(&parser)) && SUCCEEDED(parser->InitNew())) {
        constexpr size_t OriginalScriptPadding = 1024;
        std::vector<wchar_t> source(expression.length() + OriginalScriptPadding);
        wsprintfW(source.data(), L"with(Math){\n%s}", expression.wc_str());
        VARIANT value{};
        const auto parsed = parser->ParseScriptText(source.data(), nullptr, nullptr, nullptr, 0, 0,
                                                    SCRIPTTEXT_ISEXPRESSION, &value, nullptr);
        if (SUCCEEDED(parsed)) {
            if (value.vt == VT_DISPATCH && value.pdispVal) {
                wchar_t* method = const_cast<wchar_t*>(L"toString");
                DISPID identity{};
                if (SUCCEEDED(value.pdispVal->GetIDsOfNames(IID_NULL, &method, 1, LOCALE_USER_DEFAULT, &identity))) {
                    DISPPARAMS arguments{};
                    VARIANT converted{};
                    value.pdispVal->Invoke(identity, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &arguments,
                                           &converted, nullptr, nullptr);
                    VariantClear(&value);
                    value = converted;
                }
            }
            std::vector<wchar_t> text(resultCharacters);
            if (SUCCEEDED(VariantToString(value, text.data(), static_cast<UINT>(text.size()))))
                site.result.text = text.data();
        }
        VariantClear(&value);
    }
    engine->Close();
    return site.result;
}
