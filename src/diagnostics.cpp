#include "diagnostics.hpp"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <climits>
#include <regex>
#include <sstream>
#include <string_view>

namespace satr {
namespace {

using Json = nlohmann::json;

std::string Lower(std::string value) {
    for (char& c : value) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 128) c = static_cast<char>(std::tolower(byte));
    }
    return value;
}

bool Has(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

std::string Trim(const std::string& text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

std::string NormalizeSeverity(const std::string& value) {
    const std::string severity = Lower(value);
    if (severity == "warning") return "warning";
    if (severity == "note" || severity == "remark") return "note";
    return "error";
}

std::string StringField(const Json& value, const char* key) {
    const auto it = value.find(key);
    return it != value.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

int PositiveInt(const Json& value, const char* key) {
    const auto it = value.find(key);
    if (it == value.end() || !it->is_number_integer()) return 0;
    if (it->is_number_unsigned()) {
        const auto n = it->get<std::uint64_t>();
        return n > 0 && n <= INT_MAX ? static_cast<int>(n) : 0;
    }
    const auto n = it->get<std::int64_t>();
    return n > 0 && n <= INT_MAX ? static_cast<int>(n) : 0;
}

int ParsePositive(const std::string& text) {
    try {
        const auto n = std::stoll(text);
        return n > 0 && n <= INT_MAX ? static_cast<int>(n) : 0;
    } catch (...) { return 0; }
}

void ApplyLocation(Diagnostic& diagnostic, const Json& location) {
    if (!location.is_object()) return;
    diagnostic.file = StringField(location, "file");
    diagnostic.line = PositiveInt(location, "line");
    diagnostic.column = PositiveInt(location, "display-column");
    if (!diagnostic.column) diagnostic.column = PositiveInt(location, "column");
    diagnostic.byteColumn = PositiveInt(location, "byte-column");
}

void AppendJsonDiagnostic(const Json& item, std::vector<Diagnostic>& output) {
    if (item.is_array()) {
        for (const auto& child : item) AppendJsonDiagnostic(child, output);
        return;
    }
    if (!item.is_object()) return;
    const auto message = item.find("message");
    if (message != item.end() && message->is_string()) {
        auto diagnostic = ExplainDiagnostic(message->get<std::string>(),
                                            StringField(item, "kind"),
                                            StringField(item, "option"));
        const auto locations = item.find("locations");
        if (locations != item.end() && locations->is_array()) {
            for (const auto& location : *locations) {
                if (!location.is_object()) continue;
                auto caret = location.find("caret");
                if (caret == location.end() || !caret->is_object()) caret = location.find("start");
                if (caret != location.end() && caret->is_object()) {
                    ApplyLocation(diagnostic, *caret);
                    if (!diagnostic.file.empty() || diagnostic.line) break;
                }
            }
        }
        output.push_back(std::move(diagnostic));
    }
    const auto children = item.find("children");
    if (children != item.end() && children->is_array()) {
        for (const auto& child : *children) AppendJsonDiagnostic(child, output);
    }
}

// Finds a complete JSON container while respecting escaped quotes and nested values.
std::size_t JsonEnd(const std::string& text, std::size_t start) {
    std::vector<char> nesting;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '[' || c == '{') nesting.push_back(c);
        else if (c == ']' || c == '}') {
            if (nesting.empty()) return std::string::npos;
            const char expected = c == ']' ? '[' : '{';
            if (nesting.back() != expected) return std::string::npos;
            nesting.pop_back();
            if (nesting.empty()) return i + 1;
        }
    }
    return std::string::npos;
}

bool IsContextLine(const std::string& line) {
    static const std::regex excerpt(R"(^\s*(?:[0-9]+\s*\||\|).*$)");
    const auto lower = Lower(line);
    return std::regex_match(line, excerpt)
        || Has(lower, "in file included from ") || Has(lower, "in function ")
        || Has(lower, "in member function ") || Has(lower, "in instantiation of ")
        || Has(lower, "required from here") || lower == "compilation terminated.";
}

void AppendText(const std::string& raw, std::vector<Diagnostic>& output) {
    const std::string line = Trim(raw);
    if (line.empty() || IsContextLine(line)) return;
    static const std::regex compilerLocation(
        R"(^(.*):([0-9]+):([0-9]+):\s*(fatal error|error|warning|note):\s*(.*)$)");
    static const std::regex compilerLine(
        R"(^(.*):([0-9]+):\s*(fatal error|error|warning|note):\s*(.*)$)");
    static const std::regex linkerLocation(
        R"(^(.*\.(?:c|cc|cpp|cxx|h|hpp|hxx|C)):(?:\([^)]*\):)?([0-9]+):\s*(.*)$)");
    static const std::regex severityOnly(R"(^(?:.*?:\s*)?(fatal error|error|warning|note):\s*(.*)$)");
    static const std::regex optionSuffix(R"(\s*\[(-W[^\]]+)\]$)");
    std::smatch match;
    Diagnostic diagnostic;
    std::string message = line;
    std::string severity = "error";
    std::string file;
    int sourceLine = 0;
    int sourceColumn = 0;
    if (std::regex_match(line, match, compilerLocation)) {
        file = match[1].str(); sourceLine = ParsePositive(match[2].str());
        sourceColumn = ParsePositive(match[3].str()); severity = match[4].str(); message = match[5].str();
    } else if (std::regex_match(line, match, compilerLine)) {
        file = match[1].str(); sourceLine = ParsePositive(match[2].str());
        severity = match[3].str(); message = match[4].str();
    } else if (std::regex_match(line, match, linkerLocation)) {
        file = match[1].str(); sourceLine = ParsePositive(match[2].str()); message = match[3].str();
    } else if (std::regex_match(line, match, severityOnly)) {
        severity = match[1].str(); message = match[2].str();
    }
    std::string option;
    if (std::regex_search(message, match, optionSuffix)) option = match[1].str();
    diagnostic = ExplainDiagnostic(message, severity, option);
    diagnostic.original = raw; // Keep the complete unmodified source line for text diagnostics.
    diagnostic.file = file;
    diagnostic.line = sourceLine;
    diagnostic.column = sourceColumn;
    output.push_back(std::move(diagnostic));
}

bool ExpectedToken(const std::string& lower, char token) {
    if (lower.rfind("expected ", 0) != 0) return false;
    const auto end = lower.find(" before ");
    const auto at = lower.find(" at ");
    const auto boundary = std::min(end, at);
    std::string expected = lower.substr(9, boundary == std::string::npos ? boundary : boundary - 9);
    if (expected.find(" or ") != std::string::npos || expected.find(", ") != std::string::npos) return false;
    if (expected.rfind("‘", 0) == 0) expected.erase(0, 3);
    else if (!expected.empty() && (expected.front() == '\'' || expected.front() == '"')) expected.erase(0, 1);
    return !expected.empty() && expected.front() == token;
}

} // namespace

Diagnostic ExplainDiagnostic(const std::string& message, const std::string& severity,
                             const std::string& option) {
    Diagnostic diagnostic;
    diagnostic.original = message;
    diagnostic.severity = NormalizeSeverity(severity);
    diagnostic.option = option;
    const std::string text = Lower(message);
    auto explain = [&](const char* summary, const char* hint) {
        diagnostic.summary = summary;
        diagnostic.hint = hint;
    };

    if (diagnostic.severity == "note") {
        explain("ملاحظة من المترجم", "هذه معلومات إضافية عن المشكلة السابقة. راجع النص الأصلي والموضع المشار إليه.");
    } else if (Has(text, "undefined reference") || Has(text, "undefined symbol")) {
        if (Has(text, "`main'") || Has(text, "'main'") || Has(text, "‘main’") || Has(text, "winmain"))
            explain("دالة بدء البرنامج غير موجودة", "تأكد من وجود دالة main في مشروع برنامج الطرفية ومن إضافة ملفها إلى البناء. راجع نوع المشروع إذا كان المطلوب WinMain.");
        else explain("تعريف مطلوب أثناء الربط", "استخدم البرنامج رمزًا لم يجد الرابط تعريفه. أضف ملف المصدر أو المكتبة اللازمة وتحقق من تطابق التوقيع.");
    } else if (Has(text, "multiple definition") || Has(text, "redefinition of")) {
        explain("تعريف مكرر", "يوجد أكثر من تعريف للرمز. راجع التعريفات والملفات المضافة إلى البناء، واترك تعريفًا واحدًا عند الحاجة.");
    } else if (Has(text, "no such file or directory")) {
        explain("ملف مطلوب غير موجود", "تعذر العثور على الملف المذكور في الرسالة الأصلية. راجع اسمه ومساره ومسارات ملفات التضمين.");
    } else if (Has(text, "cannot find -l") || Has(text, "cannot find library")) {
        explain("مكتبة الربط غير موجودة", "راجع اسم المكتبة ومسارها وتأكد من توافقها مع المترجم ومعمارية البرنامج.");
    } else if (ExpectedToken(text, ';')) {
        explain("فاصلة منقوطة متوقعة", "يتوقع المترجم الرمز ; هنا. راجع الموضع المشار إليه والسطر الذي يسبقه قبل إضافة الرمز.");
    } else if (ExpectedToken(text, '}')) {
        explain("قوس إغلاق كتلة متوقع", "راجع توازن الأقواس { و }، بما في ذلك الكتل التي بدأت قبل هذا الموضع.");
    } else if (ExpectedToken(text, ')')) {
        explain("قوس دائري للإغلاق متوقع", "راجع توازن الأقواس ( و ) في التعبير أو استدعاء الدالة القريب من هذا الموضع.");
    } else if (ExpectedToken(text, ']')) {
        explain("قوس إغلاق فهرس متوقع", "راجع توازن الأقواس [ و ] في تعريف المصفوفة أو الوصول إلى عنصرها.");
    } else if (Has(text, "was not declared in this scope") || Has(text, "undeclared identifier") || Has(text, "undeclared (first use")) {
        explain("اسم غير معرّف في هذا النطاق", "راجع تهجئة الاسم وحالة الأحرف، وتأكد من تعريفه أو تضمين ملفه قبل استخدامه.");
    } else if (Has(text, "does not name a type") || Has(text, "unknown type name")) {
        explain("اسم نوع غير معروف", "راجع اسم النوع وتعريفه وملف الترويسة أو نطاق الأسماء الذي ينتمي إليه.");
    } else if (Has(text, "no matching function") || Has(text, "too few arguments") || Has(text, "too many arguments") || Has(text, "no match for call")) {
        explain("استدعاء دالة غير مطابق", "قارن عدد الوسائط وأنواعها بتعريف الدالة. قد تعرض ملاحظات المترجم التواقيع المتاحة.");
    } else if (Has(text, "invalid conversion") || Has(text, "cannot convert") || Has(text, "could not convert") || Has(text, "incompatible types") || Has(text, "invalid operands")) {
        explain("أنواع غير متوافقة", "راجع النوع المطلوب والنوع المستخدم في الرسالة الأصلية. صحّح البيانات أو العملية قبل اللجوء إلى تحويل صريح.");
    } else if (Has(text, "missing terminating")) {
        explain("علامة اقتباس للإغلاق مفقودة", "راجع بداية النص ونهايته، واستخدم محارف الهروب عند الحاجة إلى اقتباس داخل النص.");
    } else if (Has(text, "unused variable") || Has(text, "unused parameter") || Has(text, "unused function") || option == "-Wunused-variable" || option == "-Wunused-parameter") {
        explain("عنصر معرّف وغير مستخدم", "استخدم العنصر إذا كان مطلوبًا، أو احذف تعريفه إذا لم تعد بحاجة إليه. هذا تحذير ما لم تُعامل التحذيرات كأخطاء.");
    } else if (Has(text, "uninitialized") || option == "-Wuninitialized" || option == "-Wmaybe-uninitialized") {
        explain("قيمة قد تُستخدم قبل تهيئتها", "عيّن قيمة مناسبة للمتغير قبل قراءته، وتحقق من جميع مسارات الشروط والحلقات.");
    } else if (Has(text, "control reaches end of non-void function") || Has(text, "no return statement in function returning non-void") || option == "-Wreturn-type") {
        explain("قيمة إرجاع مطلوبة", "تأكد من أن كل مسار مناسب في الدالة يعيد قيمة تطابق نوع الإرجاع.");
    } else if (Has(text, "comparison of integer expressions of different signedness") || option == "-Wsign-compare") {
        explain("مقارنة عددين بإشارتين مختلفتين", "راجع أنواع طرفي المقارنة ومداهما، خصوصًا عند مقارنة عدد صحيح بحجم حاوية.");
    } else if (option == "-Wformat" || option.rfind("-Wformat=", 0) == 0 || (Has(text, "format ") && Has(text, "expects argument"))) {
        explain("تنسيق لا يطابق الوسائط", "راجع رموز التنسيق وأنواع الوسائط في printf أو scanf وعددها وفق الرسالة الأصلية.");
    } else if (Has(text, "misleading indentation") || option == "-Wmisleading-indentation") {
        explain("مسافة بادئة قد تضلل القارئ", "راجع الأقواس وحدود جسم الشرط أو الحلقة؛ المسافة البادئة وحدها لا تحدد كتلة التعليمات في C و C++.");
    } else if (Has(text, "deprecated") || option == "-Wdeprecated-declarations") {
        explain("استخدام عنصر مهمل", "راجع البديل المقترح في الرسالة الأصلية أو توثيق المكتبة قبل تغييره.");
    } else if (Has(text, "permission denied")) {
        explain("تعذر الوصول إلى ملف", "قد يكون الملف مستخدمًا أو غير قابل للكتابة. أوقف النسخة الجارية من البرنامج وراجع المسار والصلاحيات.");
    } else if (text.rfind("expected ", 0) == 0) {
        explain("رمز أو تركيب برمجي متوقع", "راجع الرموز أو التراكيب التي يطلبها المترجم في الرسالة الأصلية، ثم افحص هذا الموضع والسطر السابق.");
    } else if (Has(text, "expected primary-expression") || Has(text, "expected expression") || Has(text, "expected unqualified-id") || Has(text, "expected declaration") || Has(text, "expected initializer")) {
        explain("تركيب برمجي غير مكتمل أو غير متوقع", "راجع الرموز وترتيبها قرب هذا الموضع والسطر السابق. الرسالة الأصلية توضح ما كان يتوقعه المترجم.");
    } else if (Has(text, "ld returned") && Has(text, "exit status")) {
        explain("لم تكتمل مرحلة الربط", "راجع رسائل الرابط السابقة لمعرفة الرمز أو المكتبة أو الملف الذي تسبب في الفشل.");
    } else if (diagnostic.severity == "warning") {
        explain("تحذير من المترجم", "لا تتوفر ترجمة دقيقة لهذا التحذير بعد. راجع الرسالة الأصلية قبل تعديل الكود.");
    } else {
        explain("رسالة من المترجم أو الرابط", "لا تتوفر ترجمة دقيقة لهذه الرسالة بعد. النص الأصلي محفوظ أدناه دون تغيير.");
    }
    return diagnostic;
}

std::vector<Diagnostic> ParseDiagnostics(const std::string& gccOutput) {
    std::vector<Diagnostic> output;
    std::size_t position = 0;
    if (gccOutput.size() >= 3 && gccOutput.compare(0, 3, "\xEF\xBB\xBF") == 0) position = 3;
    while (position < gccOutput.size()) {
        std::size_t candidate = position;
        while (candidate < gccOutput.size() && (gccOutput[candidate] == ' ' || gccOutput[candidate] == '\t')) ++candidate;
        if (candidate < gccOutput.size() && (gccOutput[candidate] == '[' || gccOutput[candidate] == '{')) {
            const auto end = JsonEnd(gccOutput, candidate);
            if (end != std::string::npos) {
                const auto parsed = Json::parse(gccOutput.begin() + candidate, gccOutput.begin() + end, nullptr, false);
                if (!parsed.is_discarded()) {
                    const auto count = output.size();
                    AppendJsonDiagnostic(parsed, output);
                    if (output.size() == count && !parsed.empty())
                        AppendText(gccOutput.substr(position, end - position), output);
                    position = end;
                    continue;
                }
            }
        }
        const auto end = gccOutput.find('\n', position);
        std::string line = gccOutput.substr(position, end == std::string::npos ? std::string::npos : end - position);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        AppendText(line, output);
        if (end == std::string::npos) break;
        position = end + 1;
    }
    return output;
}

} // namespace satr
