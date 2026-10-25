#include "../src/java_support.hpp"
#include "../src/process.hpp"
#include "../src/script_completion.hpp"
#include <thread>
#include <iostream>
namespace fs=std::filesystem;
int checks=0,failed=0;
void Check(bool good,const std::string& name){++checks;failed+=!good;std::cout<<(good?"PASS ":"FAIL ")<<name<<std::endl;}
void Write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f<<text;}
std::pair<unsigned long,std::string> Run(const fs::path& executable,const std::vector<std::wstring>& args,const fs::path& cwd,const std::string& input=""){
    satr::Process process;std::string error,output;auto start=std::chrono::steady_clock::now();
    if(!process.Start(satr::WidePath(executable),args,satr::WidePath(cwd),error))return {999,error};
    if(!input.empty()){process.Send(input);process.EndInput();}
    while(process.Active()){output+=process.Poll();if(std::chrono::steady_clock::now()-start>std::chrono::seconds(45)){process.Stop();return {998,output};}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    output+=process.Poll();return {process.ExitCode(),output};
}
int main(int argc,char** argv){
    if(argc!=4)return 2;auto jdk=fs::absolute(fs::u8path(argv[1])),root=fs::absolute(fs::u8path(argv[2])),evidence=fs::absolute(fs::u8path(argv[3]));fs::create_directories(root);fs::create_directories(evidence);
#ifdef _WIN32
    auto javac=jdk/"bin/javac.exe",java=jdk/"bin/java.exe";
#else
    auto javac=jdk/"bin/javac",java=jdk/"bin/java";
#endif
    auto project=root/fs::u8path("ملفات Java space")/"school";auto source=project/"Main.java";auto data=root/"profile";
    std::string code="package school; import java.util.*; public class Main {public static void main(String[] args) { System.out.println(Helper.VALUE); }}";
    Write(source,code);Write(project/"Helper.java","package school; class Helper {static final int VALUE = 41;}");
    auto build=[&]{return satr::PrepareJava(source,javac,java,data,true,"");};
    auto p=build();Check(!p.hit&&p.mainClass=="school.Main"&&p.root==project.parent_path(),"package_and_unicode_source_path");
    auto compile=Run(javac,p.compileArgs,project);Check(compile.first==0,"compile_package_and_sibling_dependency: "+compile.second);
    auto result=Run(java,p.runArgs,project);Check(result.first==0&&result.second.find("41")!=std::string::npos,"run_correct_entry_class");
    Check(satr::CommitJava(p,compile.second),"commit_successful_translation");auto reused=build();Check(reused.hit,"unchanged_translation_reused_across_plan_instances");
    result=Run(java,reused.runArgs,project);Check(result.first==0&&result.second.find("41")!=std::string::npos,"cached_classes_run_correctly");
    auto time=fs::last_write_time(project/"Helper.java");Write(project/"Helper.java","package school; class Helper {static final int VALUE = 42;}");fs::last_write_time(project/"Helper.java",time);
    p=build();Check(!p.hit,"same_size_same_timestamp_dependency_change_invalidates_cache");compile=Run(javac,p.compileArgs,project);result=Run(java,p.runArgs,project);Check(compile.first==0&&result.second.find("42")!=std::string::npos,"changed_inlined_constant_recompiled");
    Write(project/"Helper.java","package school; class Helper {static final int VALUE = 43;}");Check(!satr::CommitJava(p,compile.second),"editing_during_compilation_prevents_stale_commit");
    p=build();compile=Run(javac,p.compileArgs,project);Check(compile.first==0&&satr::CommitJava(p,compile.second),"updated_dependency_cached");
    fs::remove(project/"Helper.java");p=build();Check(!p.hit,"deleted_dependency_invalidates_cache");compile=Run(javac,p.compileArgs,project);Check(compile.first!=0,"deleted_dependency_never_runs_old_class");
    auto diagnostics=satr::JavaDiagnostics(compile.second);Check(!diagnostics.empty()&&diagnostics[0].line==1&&diagnostics[0].file==source.u8string(),"javac_errors_have_exact_unicode_source_location: "+compile.second);
    Write(source,"package school; public class Main { public static void main(String[] args) { System.out.println(123) } }");p=build();compile=Run(javac,p.compileArgs,project);diagnostics=satr::JavaDiagnostics(compile.second);Check(compile.first!=0&&!diagnostics.empty()&&diagnostics[0].summary=="فاصلة منقوطة متوقعة"&&diagnostics[0].column>0,"syntax_error_and_caret_translated");
    Write(source,"package school; import java.util.Scanner; public class Main {public static void main(String[] args){Scanner in=new Scanner(System.in);int n=in.nextInt();System.out.println(\"INPUT_OK:\"+(n*2));}} ");p=build();compile=Run(javac,p.compileArgs,project);result=Run(java,p.runArgs,project,"21\n");Check(compile.first==0&&result.second.find("INPUT_OK:42")!=std::string::npos,"scanner_interactive_input");
    Check(satr::CommitJava(p,compile.second),"interactive_program_cache_commit");Write(p.cacheFile,"broken");p=build();Check(!p.hit,"corrupt_cache_falls_back_to_compilation");compile=Run(javac,p.compileArgs,project);Check(compile.first==0,"corrupt_cache_does_not_block_build");
    Check(satr::CommitJava(p,compile.second),"recover_cache_after_corruption");auto uncached=satr::PrepareJava(source,javac,java,data,false,"");Check(!uncached.hit&&!uncached.cacheable,"user_can_disable_cache");
    auto changedEnvironment=satr::PrepareJava(source,javac,java,data,true,"JAVA_TOOL_OPTIONS=-Dtest=yes");Check(!changedEnvironment.hit&&!changedEnvironment.cacheable,"custom_jvm_environment_disables_cache");
    Check(satr::java_detail::Names("// package wrong;\nclass Helper {} public class Main {String s=\"class Wrong {\"; public static void main(String... args) {}}","Main").second=="Main","comments_literals_and_varargs_main");
    Check(satr::java_detail::Names("class Helper {} class App {public static void main(String[] args){}}","Other").second=="App","non_public_entry_class_found");
    Check(!satr::java_detail::SafeClassName("../outside.class")&&!satr::java_detail::SafeClassName("C:/outside.class")&&!satr::java_detail::SafeClassName("a\\..\\outside.class"),"cached_class_paths_cannot_escape_build_directory");
    auto items=satr::JavaCompletions("print");auto item=std::find_if(items.begin(),items.end(),[](const auto& i){return i.name=="println";});
    Check(item!=items.end()&&satr::ExpandCompletion(*item,"",false).text=="println()","java_completion_inserts_parentheses");
    auto library=root/"library",lib=project.parent_path()/"lib";fs::create_directories(library/"classes");fs::create_directories(lib);
#ifdef _WIN32
    auto jar=jdk/"bin/jar.exe";
#else
    auto jar=jdk/"bin/jar";
#endif
    auto makeJar=[&](int value){Write(library/"Value.java","package ext; public class Value { public static int get(){return "+std::to_string(value)+";} }");auto c=Run(javac,{L"-J-XX:TieredStopAtLevel=1",L"-d",satr::WidePath(library/"classes"),satr::WidePath(library/"Value.java")},library);if(c.first)return false;return Run(jar,{L"--create",L"--file",satr::WidePath(library/"helper.jar"),L"-C",satr::WidePath(library/"classes"),L"."},library).first==0;};
    Check(makeJar(7),"create_library_jar");Write(lib/"helper.jar",satr::java_detail::Read(library/"helper.jar"));
    Write(source,"package school; import ext.Value; public class Main {public static void main(String[] args){System.out.println(\"LIB:\"+Value.get());}} ");
    p=build();compile=Run(javac,p.compileArgs,project);result=Run(java,p.runArgs,project);Check(compile.first==0&&result.first==0&&result.second.find("LIB:7")!=std::string::npos,"compile_and_run_with_lib_jar: "+compile.second+" | "+result.second);Check(satr::CommitJava(p,compile.second)&&build().hit,"library_program_cache_reused");
    Check(makeJar(8),"update_library_jar");Write(lib/"helper.jar",satr::java_detail::Read(library/"helper.jar"));p=build();Check(!p.hit,"changed_jar_invalidates_cache");compile=Run(javac,p.compileArgs,project);result=Run(java,p.runArgs,project);Check(compile.first==0&&result.second.find("LIB:8")!=std::string::npos,"updated_jar_behavior_observed");
    Check(satr::CommitJava(p,compile.second),"updated_jar_commit");fs::remove(lib/"helper.jar");p=build();compile=Run(javac,p.compileArgs,project);Check(!p.hit&&compile.first!=0,"removed_jar_never_runs_stale_program");
    Write(project.parent_path()/"resource.txt","RESOURCE_OK");
    Write(source,"package school; class اختبار {public static void main(String[] args) throws Exception {try(var in=اختبار.class.getResourceAsStream(\"/resource.txt\")){System.out.println(new String(in.readAllBytes(),java.nio.charset.StandardCharsets.UTF_8));}}} ");
    p=build();compile=Run(javac,p.compileArgs,project);result=Run(java,p.runArgs,project);Check(compile.first==0&&result.first==0&&result.second.find("RESOURCE_OK")!=std::string::npos,"unicode_entry_class_and_source_root_resource: "+compile.second+result.second);
    // Compare the normal javac launcher with the low-overhead compiler JVM option.
    Write(source,"package school; public class Main {public static void main(String[] args){long sum=0;for(int i=0;i<100000;i++)sum+=i;System.out.println(sum);}} ");p=build();nlohmann::json timings;
    for(const auto mode:{"normal-javac","fast-javac"}){auto args=p.compileArgs;if(std::string(mode)=="normal-javac")args.erase(args.begin());for(int i=0;i<4;++i){auto start=std::chrono::steady_clock::now();auto c=Run(javac,args,project);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();timings[mode].push_back(ms);Check(c.first==0,std::string(mode)+"_compile_"+std::to_string(i));}}
    satr::CommitJava(p,"");for(int i=0;i<5;++i){auto start=std::chrono::steady_clock::now();auto cached=build();auto c=Run(java,cached.runArgs,project);timings["cached-build-and-run"].push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());Check(cached.hit&&c.first==0&&c.second.find("4999950000")!=std::string::npos,"cached_java_exact_output_"+std::to_string(i));}
    Write(evidence/"java-timings.json",timings.dump(2));std::cout<<checks<<" Java checks, "<<failed<<" failed\n";return failed?1:0;
}
