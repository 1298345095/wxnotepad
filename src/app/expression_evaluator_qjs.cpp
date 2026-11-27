#include "app/expression_evaluator.h"
#include <quickjs.h>
#include <wx/translation.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <memory>
#include <string>

namespace {
struct ScriptValue {
    JSContext* context;
    JSValue value;
    ~ScriptValue() { JS_FreeValue(context, value); }
    void Replace(JSValue replacement) {
        JS_FreeValue(context, value);
        value = replacement;
    }
};

std::string ScriptString(JSContext* context, JSValueConst value) {
    size_t size = 0;
    const char* text = JS_ToCStringLen(context, &size, value);
    if (!text) return {};
    std::string result(text, size);
    JS_FreeCString(context, text);
    return result;
}

std::string FormatNumber(double number) {
    if (std::isnan(number)) return std::signbit(number) ? "-1.#IND" : "1.#QNAN";
    if (std::isinf(number)) return number < 0 ? "-inf" : "inf";
    if (number == 0) return std::signbit(number) ? "-0" : "0";
    constexpr int AutomationDigits = 15;
    constexpr int OriginalRoundTripDigits = 18;
    std::array<char, 128> buffer{};
    auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number, std::chars_format::general,
                                   AutomationDigits);
    std::string text(buffer.data(), formatted.ptr);
    double roundTrip = 0;
    std::from_chars(text.data(), text.data() + text.size(), roundTrip);
    if (roundTrip != number) {
        formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number, std::chars_format::general,
                                  OriginalRoundTripDigits);
        text.assign(buffer.data(), formatted.ptr);
        // propsys falls back to the legacy Windows CRT's %.18g, with a
        // three-digit exponent, when Automation's 15 digits lose information.
        if (const auto exponent = text.find('e'); exponent != std::string::npos)
            text.insert(exponent + 2, 3 - (text.size() - exponent - 2), '0');
        return text;
    }
    const bool negative = text.front() == '-';
    if (negative) text.erase(0, 1);
    int exponent = 0;
    if (const auto marker = text.find('e'); marker != std::string::npos) {
        const auto first = marker + (text[marker + 1] == '+' ? 2 : 1);
        std::from_chars(text.data() + first, text.data() + text.size(), exponent);
        text.resize(marker);
    }
    auto point = text.find('.');
    if (point == std::string::npos) point = text.size();
    const int decimalPosition = static_cast<int>(point) + exponent;
    if (point != text.size()) text.erase(point, 1);
    // Native Automation uses fixed decimal when the entire fractional/integer
    // span fits fifteen places, otherwise an uppercase scientific exponent.
    if (decimalPosition <= AutomationDigits &&
        static_cast<int>(text.size()) - std::min(0, decimalPosition) <= AutomationDigits) {
        if (decimalPosition <= 0)
            text = "0." + std::string(static_cast<size_t>(-decimalPosition), '0') + text;
        else if (decimalPosition >= static_cast<int>(text.size()))
            text.append(static_cast<size_t>(decimalPosition) - text.size(), '0');
        else
            text.insert(static_cast<size_t>(decimalPosition), 1, '.');
    } else {
        if (text.size() > 1) text.insert(1, 1, '.');
        const int scientificExponent = decimalPosition - 1;
        text += "E" + std::string(scientificExponent < 0 ? "-" : "+") + std::to_string(std::abs(scientificExponent));
    }
    return negative ? "-" + text : text;
}

size_t ScriptLineTerminator(const std::string& source, size_t position) {
    if (source[position] == '\r') return position + 1 < source.size() && source[position + 1] == '\n' ? 2 : 1;
    if (source[position] == '\n') return 1;
    if (source.compare(position, 3, "\xe2\x80\xa8") == 0 || source.compare(position, 3, "\xe2\x80\xa9") == 0) return 3;
    return 0;
}

ExpressionEvaluation ScriptError(JSContext* context, const std::string& expression) {
    ScriptValue exception{context, JS_GetException(context)};
    if (!JS_IsObject(exception.value)) {
        ExpressionEvaluation result;
        result.error = wxString::FromUTF8(ScriptString(context, exception.value));
        return result;
    }
    ScriptValue message{context, JS_GetPropertyStr(context, exception.value, "message")};
    ScriptValue stack{context, JS_GetPropertyStr(context, exception.value, "stack")};
    ExpressionEvaluation result;
    result.error =
        wxString::FromUTF8(ScriptString(context, JS_IsUndefined(message.value) ? exception.value : message.value));
    const auto location = ScriptString(context, stack.value);
    constexpr std::string_view SourceMarker = "<selection>:";
    if (const auto marker = location.find(SourceMarker); marker != std::string::npos) {
        const char* end = location.data() + location.size();
        size_t line = 0, column = 0;
        auto parsed = std::from_chars(location.data() + marker + SourceMarker.size(), end, line);
        if (parsed.ec == std::errc{} && parsed.ptr != end && *parsed.ptr == ':') {
            parsed = std::from_chars(parsed.ptr + 1, end, column);
            if (parsed.ec == std::errc{}) {
                result.line = line > 2 ? line - 2 : 0;
                result.column = column ? column - 1 : 0;
            }
        }
    }
    // The generated closing brace is not part of the selected expression.
    // Describe that EOF in user terms rather than displaying wrapper syntax.
    size_t lineStart = 0, lineEnd = 0;
    for (size_t line = 0; line <= result.line; ++line) {
        lineEnd = lineStart;
        while (lineEnd < expression.size() && !ScriptLineTerminator(expression, lineEnd))
            ++lineEnd;
        if (line == result.line || lineEnd == expression.size()) break;
        lineStart = lineEnd + ScriptLineTerminator(expression, lineEnd);
    }
    result.sourceOffset = std::min(lineStart + result.column, lineEnd);
    if (result.error == "unexpected token in expression: '}'" && result.sourceOffset == expression.size())
        result.error = _("Unexpected end of expression.");
    return result;
}
} // namespace

ExpressionEvaluation EvaluateExpression(const wxString& expression, size_t resultCharacters) {
    // No host objects, I/O modules, browser, or external executable. A new
    // runtime/context per command discards every previous variable mutation.
    const std::unique_ptr<JSRuntime, decltype(&JS_FreeRuntime)> runtime(JS_NewRuntime(), JS_FreeRuntime);
    if (!runtime) return {};
    const std::unique_ptr<JSContext, decltype(&JS_FreeContext)> context(JS_NewContext(runtime.get()), JS_FreeContext);
    if (!context) return {};
    const wxString prefix = "with(Math){\n";
    const auto wrapped = prefix + expression + "}";
    constexpr size_t OriginalSourceUtf16Limit = 1024;
    wxString originalSource;
    size_t sourceUnits = 0;
    for (const wxUniChar character : wrapped) {
        if (sourceUnits == OriginalSourceUtf16Limit) break;
        const size_t units = character.GetValue() >= 0x10000 ? 2 : 1;
        if (sourceUnits + units > OriginalSourceUtf16Limit) {
            originalSource += wxUniChar(0xfffd);
            break;
        }
        originalSource += character;
        sourceUnits += units;
    }
    const auto source = originalSource.utf8_string();
    const auto selectedSource =
        (originalSource.length() < wrapped.length() ? originalSource.Mid(prefix.length()) : expression).utf8_string();
    ScriptValue value{context.get(),
                      JS_Eval(context.get(), source.data(), source.size(), "<selection>", JS_EVAL_TYPE_GLOBAL)};
    if (JS_IsException(value.value)) return ScriptError(context.get(), selectedSource);
    if (JS_IsObject(value.value)) {
        ScriptValue method{context.get(), JS_GetPropertyStr(context.get(), value.value, "toString")};
        if (JS_IsException(method.value)) return ScriptError(context.get(), selectedSource);
        if (!JS_IsFunction(context.get(), method.value)) return {};
        const auto converted = JS_Call(context.get(), method.value, value.value, 0, nullptr);
        value.Replace(converted);
        if (JS_IsException(value.value)) return ScriptError(context.get(), selectedSource);
    }
    ExpressionEvaluation result;
    if (JS_IsNumber(value.value)) {
        double number = 0;
        JS_ToFloat64(context.get(), &number, value.value);
        result.text = wxString::FromUTF8(FormatNumber(number));
    } else if (JS_IsBool(value.value)) {
        result.text = JS_ToBool(context.get(), value.value) ? "-1" : "0";
    } else if (JS_IsString(value.value)) {
        const auto text = ScriptString(context.get(), value.value);
        // The original propsys output is passed through a null-terminated
        // WideCharToMultiByte/InsertText boundary, including BSTR embedded NULs.
        result.text = wxString::FromUTF8(text.c_str());
    }
    size_t units = 0;
    for (const wxUniChar character : result.text)
        units += character.GetValue() >= 0x10000 ? 2 : 1;
    if (units >= resultCharacters) result.text.clear();
    return result;
}
