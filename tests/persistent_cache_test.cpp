#include "../src/build_cache.hpp"
#include "../src/run_session.hpp"
#include <iostream>
#include <atomic>
int checks=0,failed=0;
void check(bool ok,const char* name){++checks;failed+=!ok;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 auto root=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(root);
 check(satr::Sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","sha_empty");
 check(satr::Sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","sha_abc");
 check(satr::Sha256(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","sha_million_a");
 check(satr::ToUtf8(satr::ToWide("path/ملف/😀"))=="path/ملف/😀","unicode_round_trip");
 auto first=satr::WidePath(root/"first.o"),second=satr::WidePath(root/"second.o");std::ofstream(satr::FsPath(first))<<"object data";
 {satr::BuildCache cache;cache.SetStorage(root/"cache");cache.Commit(first,"compiler-and-flags","expanded source","warning");}
 std::string output;
 {satr::BuildCache cache;cache.SetStorage(root/"cache");
 check(cache.CanRestore("compiler-and-flags")&&cache.Restore(second,"compiler-and-flags"),"restore_between_sessions_to_different_object_path");
 cache.Commit(second,"other-source","other preprocessed text","");
 check(cache.Restore(second,"compiler-and-flags"),"restore_after_switching_source_files");
 check(cache.UnitHit(second,"compiler-and-flags","expanded source",output)&&output=="warning","reuse_and_replay_warnings");
 check(!cache.UnitHit(second,"compiler-and-flags","changed header",output),"changed_header_invalidates");
 check(!cache.UnitHit(second,"other flags","expanded source",output),"changed_options_invalidates");
 check(!cache.UnitHit(second,"compiler-and-flags",".incbin external",output),"external_assembly_bypasses");
 std::ofstream(satr::FsPath(second))<<"changed object longer";
 check(!cache.UnitHit(second,"compiler-and-flags","expanded source",output),"changed_object_invalidates");}
 auto file=root/"cache"/(satr::Sha256("compiler-and-flags")+".cache");
 {std::fstream corrupt(file,std::ios::binary|std::ios::in|std::ios::out);corrupt.seekp(80);corrupt.put('!');}
 {satr::BuildCache cache;cache.SetStorage(root/"cache");check(!cache.Restore(second,"compiler-and-flags"),"corrupt_disk_cache_rejected");}
 auto result=satr::WidePath(root/"session.json");DWORD code=0;
 check(satr::WriteSessionFile(result,{{"id","one"},{"code",7}})&&satr::SessionResult(result,"one",code)&&code==7,"matching_session_result");
 check(!satr::SessionResult(result,"two",code),"stale_session_result_rejected");
 satr::WriteSessionFile(result,{{"id","one"},{"code","bad"}});check(!satr::SessionResult(result,"one",code),"malformed_result_rejected");
 check(!satr::WriteSessionFile(result,{{"huge",std::string(140000,'x')}}),"oversized_session_rejected");
 std::atomic<bool> done{false};std::atomic<int> failures{0};
 std::thread reader([&]{while(!done){auto value=satr::ReadSessionFile(result);if(value.is_discarded())++failures;}});
 for(int i=0;i<100;++i)if(!satr::WriteSessionFile(result,{{"id",std::to_string(i)},{"code",0}}))++failures;
 done=true;reader.join();check(failures==0,"concurrent_result_reads_allow_atomic_replacement");
 std::ofstream(root/"cache"/"student.cpp")<<"do not remove";
 for(int i=0;i<140;++i)std::ofstream(root/"cache"/(satr::Sha256(std::to_string(i))+".cache"))<<"old";
 {satr::BuildCache cache;cache.SetStorage(root/"cache");cache.Commit(first,"new-entry","source","note");}
 size_t count=0;for(const auto& file:std::filesystem::directory_iterator(root/"cache"))if(file.path().extension()==".cache")++count;
 check(count<=128&&std::filesystem::exists(root/"cache"/"student.cpp"),"bounded_cache_preserves_unrelated_files");
 std::cout<<checks<<" checks, "<<failed<<" failed\n";return failed?1:0;
}
