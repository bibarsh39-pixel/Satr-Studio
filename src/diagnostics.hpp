#pragma once

#include <string>
#include <vector>

namespace satr {

struct Diagnostic {
    std::string severity;
    std::string file;
    std::string original;
    std::string summary;
    std::string hint;
    std::string option;
    int line = 0;
    int column = 0;       // GCC display column (1-based), if available.
    int byteColumn = 0;   // GCC UTF-8 byte column (1-based), if available.
};

// All strings are UTF-8. Unrecognized messages retain the exact original text.
Diagnostic ExplainDiagnostic(const std::string& message,
                             const std::string& severity = "error",
                             const std::string& option = "");

// Accepts GCC JSON arrays/objects, sequential arrays, and linker/compiler text.
// Invalid or missing source positions remain zero; never fabricate a position.
std::vector<Diagnostic> ParseDiagnostics(const std::string& gccOutput);

} // namespace satr
