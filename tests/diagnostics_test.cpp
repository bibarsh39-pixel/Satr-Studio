#include "diagnostics.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void TestJsonLocationsAndNotes() {
    const std::string input = R"json([
      {"kind":"error","message":"expected ';' before 'return'","locations":[
        {"caret":{"file":"C:/study/مشروع/main.cpp","line":5,"column":9,"display-column":9,"byte-column":13}}],
       "children":[{"kind":"note","message":"to match this '(' key","locations":[
        {"caret":{"file":"C:/study/مشروع/main.cpp","line":2,"column":3,"byte-column":3}}]}]},
      {"kind":"warning","option":"-Wunused-variable","message":"unused variable 'x'","locations":[]}
    ])json";
    const auto results = satr::ParseDiagnostics(input);
    Check(results.size() == 3, "JSON should flatten parent, child note and warning");
    Check(results[0].file == "C:/study/مشروع/main.cpp", "Arabic Windows path must be preserved");
    Check(results[0].line == 5 && results[0].column == 9 && results[0].byteColumn == 13, "Display and UTF-8 byte columns must differ correctly");
    Check(results[0].summary == "فاصلة منقوطة متوقعة", "Semicolon pattern should have specific Arabic explanation");
    Check(results[0].original == "expected ';' before 'return'", "Compiler message must remain exact");
    Check(results[1].severity == "note" && results[1].line == 2, "Child note must use its own source location");
    Check(results[2].option == "-Wunused-variable" && results[2].line == 0, "Warning option must be retained and missing location must remain absent");
}

void TestStreamsAndEscaping() {
    const std::string input = "[]\r\n" R"json([{"kind":"error","message":"name 'a[\\\"]' was not declared in this scope","locations":[]}])json"
        "\n" R"json({"kind":"warning","message":"unused variable 'y'","option":"-Wunused-variable"})json"
        "\ncollect2.exe: error: ld returned 1 exit status\n";
    const auto results = satr::ParseDiagnostics(input);
    Check(results.size() == 3, "Multiple arrays/objects and linker text must coexist");
    Check(results[0].summary == "اسم غير معرّف في هذا النطاق", "Bracket and escaped quote in JSON string must not break parser");
    Check(results[1].severity == "warning", "Sequential JSON object must parse");
    Check(results[2].summary == "لم تكتمل مرحلة الربط", "Linker summary should be identified");
    Check(satr::ParseDiagnostics("\xEF\xBB\xBF[]\n").empty(), "BOM and empty success array must not create errors");
}

void TestFallbacks() {
    auto results = satr::ParseDiagnostics("C:\\User Files\\main.cpp:12:7: error: 'value' was not declared in this scope\n"
        " 12 | value = 4;\n"
        "    | ^~~~~\n"
        "C:/work/main.cpp:17: warning: unused variable 'x' [-Wunused-variable]\n"
        "C:/work/main.cpp:20: undefined reference to `thing()'\n");
    Check(results.size() == 3, "Source excerpts must not appear as duplicate diagnostics");
    Check(results[0].file == "C:\\User Files\\main.cpp" && results[0].line == 12 && results[0].column == 7, "Text path must allow drive colon, spaces and line/column");
    Check(results[0].byteColumn == 0, "Text fallback must not invent UTF-8 byte column");
    Check(results[1].severity == "warning" && results[1].option == "-Wunused-variable", "Text warning and option must parse");
    Check(results[2].file == "C:/work/main.cpp" && results[2].line == 20, "Linker source location must be retained");
    Check(results[2].summary == "تعريف مطلوب أثناء الربط", "Undefined reference must be explained");
    const auto unknown = satr::ExplainDiagnostic("unrecognized vendor message \"raw\"\nsecond line");
    Check(unknown.original == "unrecognized vendor message \"raw\"\nsecond line", "Unknown original text must be byte-for-byte preserved");
    Check(unknown.summary == "رسالة من المترجم أو الرابط", "Unknown messages must not fabricate a translation");
    results = satr::ParseDiagnostics("[broken JSON\nlinker unfamiliar failure\n");
    Check(results.size() == 2, "Malformed data must remain inspectable rather than disappear or crash");
}

void TestInvalidLocationData() {
    auto results = satr::ParseDiagnostics(R"json([{"kind":"error","message":"test","locations":[{"caret":{"file":"a.cpp","line":-1,"column":"2","display-column":99999999999999999,"byte-column":0}}],"children":[{"kind":"note","message":"context"}]}])json");
    Check(results.size() == 2, "Malformed numeric position should not discard messages");
    Check(results[0].line == 0 && results[0].column == 0 && results[0].byteColumn == 0, "Malformed/overflow/negative positions must remain absent");
    Check(results[1].file.empty() && results[1].line == 0, "Child notes must not inherit unverified parent locations");
    results = satr::ParseDiagnostics(R"json([{"kind":"error","message":"test","locations":[{"start":{"file":"fallback.cpp","line":3,"column":4}}]}])json");
    Check(results[0].file == "fallback.cpp" && results[0].line == 3, "Range start should be used when no caret is present");
}

void TestClassificationBoundaries() {
    Check(satr::ExplainDiagnostic("expected ')' before ';' token").summary == "قوس دائري للإغلاق متوقع", "Delimiter after before must not be mistaken for expected token");
    Check(satr::ExplainDiagnostic("expected ‘;’ before ‘}’ token").summary == "فاصلة منقوطة متوقعة", "GCC Unicode quotation should be handled");
    Check(satr::ExplainDiagnostic("expected ',' or ';' before 'return'").summary == "رمز أو تركيب برمجي متوقع", "Alternative expected tokens must not be translated as a definite missing semicolon");
    Check(satr::ExplainDiagnostic("expected '}' at end of input").summary == "قوس إغلاق كتلة متوقع", "Missing closing brace should be explained");
    Check(satr::ExplainDiagnostic("maththing.h: No such file or directory").summary == "ملف مطلوب غير موجود", "Missing include should be explained");
    Check(satr::ExplainDiagnostic("invalid conversion from 'const char*' to 'int'").summary == "أنواع غير متوافقة", "Conversion mismatch should be explained");
    Check(satr::ExplainDiagnostic("no matching function for call to 'f()'").summary == "استدعاء دالة غير مطابق", "Call mismatch should be explained");
    Check(satr::ExplainDiagnostic("undefined reference to `main'").summary == "دالة بدء البرنامج غير موجودة", "Missing main should be explained");
    Check(satr::ExplainDiagnostic("multiple definition of `main'").summary == "تعريف مكرر", "Duplicate definition should be explained");
    Check(satr::ExplainDiagnostic("unused variable 'x'", "warning", "-Wunused-variable").severity == "warning", "Warnings must not become errors");
    Check(satr::ExplainDiagnostic("control reaches end of non-void function", "warning").summary == "قيمة إرجاع مطلوبة", "Return warning should be explained");
    Check(satr::ExplainDiagnostic("something unusual", "warning").summary == "تحذير من المترجم", "Unknown warning should remain honest");
}

// Optional files are actual GCC -fdiagnostics-format=json output captured by the caller.
// This path ensures our parser also accepts the installed compiler's real output.
void TestActualCompilerOutput(const char* path) {
    std::ifstream file(path, std::ios::binary);
    Check(file.good(), "Actual GCC fixture must exist");
    const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto results = satr::ParseDiagnostics(contents);
    Check(!results.empty(), "Actual erroneous source must produce diagnostics");
    bool sourceError = false;
    for (const auto& diagnostic : results) {
        Check(!diagnostic.original.empty() && !diagnostic.summary.empty(), "Actual messages must retain original and Arabic summary");
        if (diagnostic.severity == "error" && diagnostic.line > 0 && !diagnostic.file.empty()) sourceError = true;
    }
    Check(sourceError, "Actual GCC fixture must retain at least one navigable source error");
}
}

int main(int argc, char** argv) {
    try {
        TestJsonLocationsAndNotes();
        TestStreamsAndEscaping();
        TestFallbacks();
        TestInvalidLocationData();
        TestClassificationBoundaries();
        for (int i = 1; i < argc; ++i) TestActualCompilerOutput(argv[i]);
        std::cout << "PASS: " << checks << " diagnostic checks\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL after " << checks << " checks: " << exception.what() << '\n';
        return 1;
    }
}
