#pragma once
#include <cstdint>
namespace satr {
inline const char* RunMessage(std::uint32_t code){
    switch(code){
    case 0:return "Program finished successfully.";
    case 0xC0000005u:return "Runtime error: access violation (invalid memory access).";
    case 0xC0000094u:return "Runtime error: integer division by zero.";
    case 0xC00000FDu:return "Runtime error: stack overflow (check recursion and large local arrays).";
    case 0xC000001Du:return "Runtime error: illegal instruction.";
    case 0xC0000135u:return "Startup error: a required DLL was not found.";
    case 0xC000013Au:return "Program interrupted.";
    default:return "Program returned a non-zero exit code.";
    }
}
}
