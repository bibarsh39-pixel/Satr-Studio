#include "../src/repair.hpp"
#include "../src/run_messages.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
namespace fs=std::filesystem;
int checks=0,failed=0;
void Check(bool value,const char* label){++checks;if(!value)++failed;std::cout<<(value?"PASS ":"FAIL ")<<label<<std::endl;}
bool Finish(satr::RepairTask& task){auto start=GetTickCount64();ULONGLONG longest=0;while(task.Busy()&&GetTickCount64()-start<160000){auto t=GetTickCount64();task.Poll();longest=std::max(longest,GetTickCount64()-t);Sleep(15);}std::cout<<"Task: "<<static_cast<int>(task.Status())<<", AI="<<task.UsedAI()<<", seconds="<<task.Seconds()<<", longest poll ms="<<longest<<", error="<<task.Error()<<"\n";Check(longest<500,"poll_does_not_block_for_inference");return task.Status()==satr::RepairTask::State::Ready;}
int wmain(int argc,wchar_t** argv){
    if(argc!=4)return 2;
    satr::RepairConfig config;config.compiler=argv[1];auto app=fs::absolute(argv[2]),work=fs::absolute(argv[3]);fs::create_directories(work);config.directory=work;config.scratch=work/L"scratch";config.runtime=app/L"assistant"/L"llama-completion.exe";config.model=app/L"assistant"/L"qwen2.5-coder-0.5b-instruct-q4_k_m.gguf";config.c=true;
    using J=satr::RepairJson;
    auto pos=[](int line,int column){return J{{"file","<stdin>"},{"line",line},{"byte-column",column}};};
    Check(satr::RepairOffset("// عربي\n\tint x;",pos(2,2))==std::optional<size_t>(13),"utf8_and_tab_byte_position");
    Check(!satr::RepairOffset("abc",pos(0,1)),"invalid_line_rejected");
    Check(!satr::RepairOffset("abc",pos(1,8)),"out_of_bounds_rejected");
    Check(!satr::RepairOffset("ع",pos(1,2)),"split_utf8_rejected");
    auto other=pos(1,1);other["file"]="header.h";Check(!satr::RepairOffset("abc",other),"included_file_edits_rejected");
    J edit={{"start",pos(1,2)},{"next",pos(1,3)},{"string","X"}};
    J diag=J::array({{{"kind","error"},{"fixits",J::array({edit,edit})}}});
    Check(satr::CompilerRepair("abc",diag.dump())=="aXc","duplicate_fixits_applied_once");
    auto overlap=edit;overlap["string"]="Y";diag[0]["fixits"].push_back(overlap);Check(satr::CompilerRepair("abc",diag.dump())=="abc","conflicting_edits_rejected");
    Check(satr::CompilerRepair("abc","broken json")=="abc","malformed_compiler_response_ignored");
    Check(satr::ExtractRepair("```c\nint main(){return 0;}\n```","bad")=="int main(){return 0;}\n","extract_complete_model_source");
    Check(satr::ExtractRepair("```c\ntruncated","bad").empty(),"truncated_model_reply_rejected");
    Check(satr::ExtractRepair("No change needed","bad").empty(),"prose_model_reply_rejected");
    Check(satr::ExtractRepair("```c\na\nb\n```","x\r\n")=="a\r\nb\r\n","model_output_preserves_crlf");
    Check(satr::ExtractRepair("```c\r\nint x;\r\n```","x\n")=="int x;\n","windows_model_newlines_normalized");
    Check(std::string(satr::RunMessage(0)).find("successfully")!=std::string::npos,"console_success_english");
    Check(std::string(satr::RunMessage(0xc0000005u)).find("access violation")!=std::string::npos,"console_access_violation_english");
    Check(std::string(satr::RunMessage(0xc0000094u)).find("division by zero")!=std::string::npos,"console_division_by_zero_english");
    Check(std::string(satr::RunMessage(7)).find("non-zero")!=std::string::npos,"console_custom_exit_code_not_mislabelled");
    satr::RepairTask clean;Check(clean.Start(config,"int main(void){return 0;}\n"),"clean_source_starts");Finish(clean);Check(clean.Status()==satr::RepairTask::State::Clean&&!clean.UsedAI(),"clean_source_does_not_load_model");
    satr::RepairTask semicolon;semicolon.Start(config,"// عربي\nint main(void){return 0}\n");Check(Finish(semicolon)&&semicolon.Candidate().find("return 0;")!=std::string::npos&&!semicolon.UsedAI(),"missing_semicolon_compiler_fix");
    satr::RepairTask typo;typo.Start(config,"int main(void){int number=1;return numbr;}\n");Check(Finish(typo)&&typo.Candidate().find("return number;")!=std::string::npos&&!typo.UsedAI(),"misspelled_variable_compiler_fix");
    satr::RepairTask larger;larger.Start(config,"/*"+std::string(10000,'x')+"*/\nint main(void){return 0}\n");Check(Finish(larger)&&!larger.UsedAI(),"compiler_repair_sends_source_larger_than_pipe_chunk");
    std::ofstream(work/L"local.h")<<"#define VALUE 7\n";
    satr::RepairTask header;header.Start(config,"#include \"local.h\"\nint main(void){int x=VALUE;return x}\n");Check(Finish(header)&&!header.UsedAI(),"quoted_header_resolves_from_original_directory");
    auto noAI=config;noAI.allowAI=false;satr::RepairTask disabled;disabled.Start(noAI,"int main(void){int value=3 return value;}\n");Finish(disabled);Check(disabled.Status()==satr::RepairTask::State::Failed&&!disabled.UsedAI(),"disabled_ai_never_starts_model");
    satr::RepairTask ai;ai.Start(config,"#include <stdio.h>\nint main(void) {\n    int value = 3\n    printf(\"%d\\n\", value);\n    return 0;\n}\n");bool ready=Finish(ai);std::cout<<"AI output:\n"<<ai.ModelOutput()<<"\nAI candidate:\n"<<ai.Candidate()<<std::endl;Check(ready&&ai.UsedAI()&&ai.Candidate().find("printf")!=std::string::npos&&ai.Candidate().find("value = 3;")!=std::string::npos,"real_local_model_fixes_c_and_passes_gcc");
    satr::RepairTask cancelled;cancelled.Start(config,"int main(void){int value=3 return value;}\n");auto start=GetTickCount64();while(cancelled.Busy()&&!cancelled.UsedAI()&&GetTickCount64()-start<10000){cancelled.Poll();Sleep(15);}Check(cancelled.UsedAI(),"cancel_test_model_started");cancelled.Cancel();Check(!cancelled.Busy()&&cancelled.Status()==satr::RepairTask::State::Cancelled,"cancel_stops_model");Check(!fs::exists(fs::path(config.scratch)/L"repair-prompt.txt"),"prompt_removed_after_cancel");
    Check(!fs::exists(fs::path(config.scratch)/L"repair-prompt.txt"),"prompt_removed_after_completion");
    std::cout<<checks<<" repair checks, "<<failed<<" failed\n";return failed?1:0;
}
