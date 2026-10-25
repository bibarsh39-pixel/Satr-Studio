#pragma once
#include "completion.hpp"
namespace satr {
enum class CompletionKind { Word=0, Function=1, Type=2, Variable=3, Namespace=4, Header=5 };
struct CompletionItem {
    std::string name,insert,signature,header;
    CompletionKind kind=CompletionKind::Word;
};
inline std::set<std::string> Words(const std::string& text){std::istringstream stream(text);std::set<std::string> result;std::string word;while(stream>>word)result.insert(word);return result;}
inline const std::set<std::string>& CFunctions(){
    static auto words=Words("printf fprintf sprintf snprintf vprintf vfprintf vsprintf vsnprintf scanf fscanf sscanf vscanf vfscanf vsscanf puts putchar putc fputs fputc getchar getc fgets fgetc ungetc fopen freopen fclose fflush fread fwrite fseek ftell rewind fgetpos fsetpos feof ferror clearerr perror remove rename tmpfile tmpnam setbuf setvbuf malloc calloc realloc free aligned_alloc abort exit atexit at_quick_exit quick_exit _Exit getenv system atoi atol atoll atof strtol strtoll strtoul strtoull strtod strtof strtold rand srand bsearch qsort abs labs llabs div ldiv lldiv mblen mbtowc wctomb mbstowcs wcstombs strlen strcmp strncmp strcpy strncpy strcat strncat strchr strrchr strstr strtok strspn strcspn strpbrk strcoll strxfrm strerror memcpy memmove memset memcmp memchr isalnum isalpha isblank iscntrl isdigit isgraph islower isprint ispunct isspace isupper isxdigit tolower toupper sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh exp exp2 expm1 log log10 log2 log1p pow sqrt cbrt hypot ceil floor trunc round lround llround nearbyint rint lrint llrint fabs fmod remainder remquo fmax fmin fdim copysign nextafter nexttoward frexp ldexp modf scalbn scalbln ilogb logb erf erfc tgamma lgamma isnan isinf isfinite isnormal signbit fpclassify time clock difftime mktime localtime gmtime strftime asctime ctime timespec_get setlocale localeconv assert signal raise setjmp longjmp offsetof va_start va_arg va_copy va_end wprintf fwprintf swprintf wscanf fwscanf swscanf wcslen wcscpy wcsncpy wcscmp wcsncmp wcscat wcsncat wcschr wcsrchr wcsstr wcstok wmemcpy wmemmove wmemset wmemcmp btowc wctob mbrlen mbrtowc wcrtomb mbsrtowcs wcsrtombs iswalpha iswdigit towlower towupper thrd_create thrd_join thrd_sleep mtx_init mtx_lock mtx_unlock cnd_wait cnd_signal atomic_load atomic_store atomic_fetch_add atomic_exchange atomic_compare_exchange_strong feclearexcept fetestexcept fegetround fesetround");return words;
}
inline const std::set<std::string>& Members(){
    static auto words=Words("begin end cbegin cend rbegin rend size empty data front back at push_back emplace_back pop_back push_front pop_front push pop top insert emplace erase clear reserve resize capacity length substr append replace find rfind find_first_of find_last_of compare contains count lower_bound upper_bound equal_range bucket_count load_factor join detach joinable seekg seekp tellg tellp read write good fail bad eof ignore peek putback get put open close is_open reset release get_deleter use_count expired lock unlock try_lock wait notify_one notify_all");return words;
}
inline bool Callable(const std::string& word){
    static auto cpp=Words("getline to_string to_wstring stoi stol stoll stoul stoull stof stod stold format print println move forward swap exchange make_pair make_tuple tie get apply visit make_optional sort stable_sort partial_sort nth_element reverse rotate shuffle unique unique_copy remove_if remove_copy remove_copy_if replace_if replace_copy copy copy_if copy_n copy_backward move_backward fill fill_n generate generate_n transform for_each for_each_n find_if find_if_not find_end find_first_of adjacent_find count_if mismatch equal search search_n all_of any_of none_of is_sorted is_sorted_until binary_search merge inplace_merge includes set_union set_intersection set_difference set_symmetric_difference next_permutation prev_permutation min max minmax clamp min_element max_element minmax_element distance advance next prev iter_swap accumulate reduce transform_reduce inner_product partial_sum adjacent_difference iota gcd lcm midpoint make_unique make_shared allocate_shared static_pointer_cast dynamic_pointer_cast const_pointer_cast reinterpret_pointer_cast current_exception rethrow_exception terminate async sleep_for sleep_until yield get_id hardware_concurrency duration_cast time_point_cast now exists create_directory create_directories current_path file_size is_directory is_regular_file copy_file remove_all absolute canonical weakly_canonical relative regex_match regex_search regex_replace setw setprecision setfill quoted sync_with_stdio");
    return CFunctions().count(word)||Members().count(word)||cpp.count(word);
}
inline std::string HeaderFor(const std::string& word){
    static const std::vector<std::pair<std::string,std::set<std::string>>> groups={
      {"stdio.h",Words("printf fprintf sprintf snprintf vprintf vfprintf vsprintf vsnprintf scanf fscanf sscanf vscanf vfscanf vsscanf puts putchar putc fputs fputc getchar getc fgets fgetc ungetc fopen freopen fclose fflush fread fwrite fseek ftell rewind fgetpos fsetpos feof ferror clearerr perror remove rename tmpfile tmpnam setbuf setvbuf")},
      {"stdlib.h",Words("malloc calloc realloc free aligned_alloc abort exit atexit at_quick_exit quick_exit _Exit getenv system atoi atol atoll atof strtol strtoll strtoul strtoull strtod strtof strtold rand srand bsearch qsort abs labs llabs div ldiv lldiv")},
      {"string.h",Words("strlen strcmp strncmp strcpy strncpy strcat strncat strchr strrchr strstr strtok strspn strcspn strpbrk strcoll strxfrm strerror memcpy memmove memset memcmp memchr")},
      {"math.h",Words("sin cos tan asin acos atan atan2 sinh cosh tanh exp exp2 log log10 log2 pow sqrt cbrt hypot ceil floor trunc round fabs fmod fmax fmin")},
      {"ctype.h",Words("isalnum isalpha isblank iscntrl isdigit isgraph islower isprint ispunct isspace isupper isxdigit tolower toupper")},
      {"algorithm",Words("sort stable_sort find find_if count count_if reverse rotate copy fill transform for_each min max clamp all_of any_of none_of")},
      {"memory",Words("make_unique make_shared allocate_shared unique_ptr shared_ptr weak_ptr")},
      {"numeric",Words("accumulate reduce transform_reduce inner_product partial_sum adjacent_difference iota gcd lcm midpoint")},
      {"string",Words("string wstring getline to_string stoi stol stoll stoul stoull stof stod stold")},
      {"vector",Words("vector")},{"iostream",Words("cin cout cerr clog")},{"fstream",Words("ifstream ofstream fstream")}
    };for(const auto& group:groups)if(group.second.count(word))return group.first;return {};
}
inline CompletionItem LocalItem(const std::string& word){
    CompletionItem item;item.name=item.insert=word;item.header=HeaderFor(word);
    if(Callable(word)){item.kind=CompletionKind::Function;auto tip=FunctionTips().find(word);item.signature=tip!=FunctionTips().end()?tip->second:word+"(...)";}
    else if(word=="std"||word=="chrono"||word=="filesystem"||word=="this_thread"||word=="ranges"||word=="views")item.kind=CompletionKind::Namespace;
    return item;
}
inline bool AvailableWord(const std::string& word,bool c,int standard){
    static auto removed=Words("gets random_shuffle atomic_cancel atomic_commit atomic_noexcept synchronized _BitInt _Decimal32 _Decimal64 _Decimal128 typeof typeof_unqual strdup strndup");
    if(removed.count(word))return false;
    static auto cpp20=Words("concept requires consteval constinit co_await co_return co_yield char8_t u8string span format jthread ranges views filter take drop take_while drop_while split counted subrange sentinel input_iterator output_iterator forward_iterator bidirectional_iterator random_access_iterator contiguous_iterator midpoint contains");
    static auto cOnly=Words("_Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary _Noreturn _Static_assert _Thread_local restrict");
    if(!c)return !cOnly.count(word)&&!(standard<20&&cpp20.count(word))&&!(standard<23&&(word=="print"||word=="println"));
    static auto cWords=Words("auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while _Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary _Noreturn _Static_assert _Thread_local _Pragma include define undef ifdef ifndef endif elif pragma error __FILE__ __LINE__ __DATE__ __TIME__ NULL EOF EXIT_SUCCESS EXIT_FAILURE stdin stdout stderr size_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t FILE va_list errno ERANGE EDOM INT_MAX INT_MIN UINT_MAX CHAR_BIT INT64_MAX bool true false");
    return cWords.count(word)||CFunctions().count(word);
}
inline std::vector<CompletionItem> LocalCompletions(const std::string& prefix,const std::string& source,bool c,int standard,bool member=false,bool qualified=false){
    auto words=Complete(prefix,source);std::set<std::string> all(words.begin(),words.end());
    for(const auto& word:CFunctions())if(word.size()>prefix.size()&&word.compare(0,prefix.size(),prefix)==0)all.insert(word);
    std::vector<CompletionItem> result;
    for(const auto& word:all){if(!AvailableWord(word,c,standard)){// Keep user-defined names, but never reintroduce catalog words disallowed for this language.
            static auto catalog=Words(CompletionWords());if(catalog.count(word)||CFunctions().count(word))continue;
        }
        auto item=LocalItem(word);if(member&&!Members().count(word))continue;
        if(qualified&&item.kind==CompletionKind::Word&&HeaderFor(word).empty())continue;
        result.push_back(std::move(item));
    }return result;
}
inline std::vector<CompletionItem> HeaderCompletions(const std::string& prefix,bool c,int standard){
    static auto cHeaders=Words("assert.h complex.h ctype.h errno.h fenv.h float.h inttypes.h iso646.h limits.h locale.h math.h setjmp.h signal.h stdalign.h stdarg.h stdatomic.h stdbool.h stddef.h stdint.h stdio.h stdlib.h stdnoreturn.h string.h tgmath.h threads.h time.h uchar.h wchar.h wctype.h");
    static auto cppHeaders=Words("algorithm any array atomic bit bitset cassert cctype cerrno cfenv cfloat charconv chrono cinttypes climits clocale cmath codecvt compare complex concepts condition_variable coroutine csetjmp csignal cstdarg cstddef cstdint cstdio cstdlib cstring ctime cuchar cwchar cwctype deque exception execution filesystem format forward_list fstream functional future initializer_list iomanip ios iosfwd iostream istream iterator latch limits list map memory memory_resource mutex new numbers numeric optional ostream print queue random ranges ratio regex scoped_allocator semaphore set shared_mutex source_location span sstream stack stdexcept stop_token streambuf string string_view syncstream system_error thread tuple type_traits typeindex typeinfo unordered_map unordered_set utility valarray variant vector version");
    static auto modern=Words("bit compare concepts coroutine format latch numbers ranges semaphore source_location span stop_token syncstream");
    auto words=cHeaders;if(!c)words.insert(cppHeaders.begin(),cppHeaders.end());std::vector<CompletionItem> result;
    for(const auto& word:words){if(!c&&((standard<20&&modern.count(word))||(standard<23&&word=="print")))continue;if(word.compare(0,prefix.size(),prefix)==0&&word.size()>prefix.size())result.push_back({word,word,"",word,CompletionKind::Header});}return result;
}
inline std::string RemoveSnippetFields(const std::string& text){
    std::string result;for(size_t i=0;i<text.size();++i){if(text[i]=='$'&&i+1<text.size()){
        if(text[i+1]=='{'){int depth=1;i+=2;for(;i<text.size()&&depth;++i){if(text[i]=='{')++depth;if(text[i]=='}')--depth;}--i;continue;}
        if(std::isdigit(static_cast<unsigned char>(text[i+1]))){while(i+1<text.size()&&std::isdigit(static_cast<unsigned char>(text[i+1])))++i;continue;}
    }if(text[i]=='\\'&&i+1<text.size()&&(text[i+1]=='$'||text[i+1]=='}'||text[i+1]=='\\'))++i;result+=text[i];}return result;
}
struct CompletionEdit {std::string text;size_t caret=0;};
inline CompletionEdit ExpandCompletion(const CompletionItem& item,const std::string& after,bool address=false,char headerClose='>'){
    std::string text=item.insert.empty()?item.name:item.insert;
    if(item.kind==CompletionKind::Function){auto paren=text.find('(');if(paren!=std::string::npos)text.resize(paren);text=RemoveSnippetFields(text);
        if(address)return {text,text.size()};
        size_t whitespace=0;while(whitespace<after.size()&&(after[whitespace]==' '||after[whitespace]=='\t'))++whitespace;
        if(whitespace<after.size()&&after[whitespace]=='(')return {text,text.size()+whitespace+1};
        auto angle=text.find("<>");text+="()";return {text,angle!=std::string::npos?angle+1:text.size()-1};
    }
    text=RemoveSnippetFields(text);
    if(item.kind==CompletionKind::Namespace){if(text.size()<2||text.substr(text.size()-2)!="::")text+="::";if(after.rfind("::",0)==0)text.resize(text.size()-2);return {text,text.size()+(after.rfind("::",0)==0?2:0)};}
    if(item.kind==CompletionKind::Header){if(after.empty()||after[0]!=headerClose)text+=headerClose;return {text,text.size()+(!after.empty()&&after[0]==headerClose?1:0)};}
    auto angle=text.find("<>");return {text,angle==std::string::npos?text.size():angle+1};
}
}
