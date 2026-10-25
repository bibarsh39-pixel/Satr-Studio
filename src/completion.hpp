#pragma once
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace satr {
inline const std::string& CompletionWords() {
    static const std::string words =
      "alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto bitand bitor bool break case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false final float for friend goto if import inline int long module mutable namespace new noexcept not not_eq nullptr operator or or_eq override private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch synchronized template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq "
      "_Alignas _Alignof _Atomic _BitInt _Bool _Complex _Decimal32 _Decimal64 _Decimal128 _Generic _Imaginary _Noreturn _Static_assert _Thread_local typeof typeof_unqual _Pragma restrict "
      "include define undef ifdef ifndef endif elif pragma error warning __FILE__ __LINE__ __DATE__ __TIME__ __cplusplus NULL EOF EXIT_SUCCESS EXIT_FAILURE stdin stdout stderr size_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t FILE va_list va_start va_arg va_copy va_end "
      "printf fprintf sprintf snprintf vprintf vfprintf vsprintf vsnprintf scanf fscanf sscanf vscanf vfscanf vsscanf puts putchar putc fputs fputc gets getchar getc fgets fgetc ungetc fopen freopen fclose fflush fread fwrite fseek ftell rewind fgetpos fsetpos feof ferror clearerr perror remove rename tmpfile tmpnam setbuf setvbuf "
      "malloc calloc realloc free aligned_alloc abort exit atexit at_quick_exit quick_exit _Exit getenv system atoi atol atoll atof strtol strtoll strtoul strtoull strtod strtof strtold rand srand bsearch qsort abs labs llabs div ldiv lldiv mblen mbtowc wctomb mbstowcs wcstombs "
      "strlen strcmp strncmp strcpy strncpy strcat strncat strchr strrchr strstr strtok strspn strcspn strpbrk strcoll strxfrm strerror memcpy memmove memset memcmp memchr strdup strndup "
      "isalnum isalpha isblank iscntrl isdigit isgraph islower isprint ispunct isspace isupper isxdigit tolower toupper "
      "sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh exp exp2 expm1 log log10 log2 log1p pow sqrt cbrt hypot ceil floor trunc round lround llround nearbyint rint lrint llrint fabs fmod remainder remquo fmax fmin fdim copysign nextafter nexttoward frexp ldexp modf scalbn scalbln ilogb logb erf erfc tgamma lgamma isnan isinf isfinite isnormal signbit fpclassify "
      "time clock difftime mktime localtime gmtime strftime asctime ctime timespec_get setlocale localeconv assert static_assert signal raise setjmp longjmp offsetof errno ERANGE EDOM INT_MAX INT_MIN UINT_MAX CHAR_BIT INT64_MAX "
      "std cin cout cerr clog endl flush ws getline ostream istream iostream ofstream ifstream fstream string wstring u8string u16string u32string string_view wstring_view to_string to_wstring stoi stol stoll stoul stoull stof stod stold format print println "
      "vector array deque list forward_list set multiset map multimap unordered_set unordered_multiset unordered_map unordered_multimap stack queue priority_queue pair tuple optional variant any span bitset function reference_wrapper initializer_list move forward swap exchange make_pair make_tuple tie get apply visit make_optional nullopt monostate "
      "begin end cbegin cend rbegin rend size empty data front back at push_back emplace_back pop_back push_front pop_front push pop top insert emplace erase clear reserve resize capacity length substr append replace find rfind find_first_of find_last_of compare contains count lower_bound upper_bound equal_range bucket_count load_factor "
      "sort stable_sort partial_sort nth_element reverse rotate shuffle random_shuffle unique unique_copy remove_if remove_copy remove_copy_if replace_if replace_copy copy copy_if copy_n copy_backward move_backward fill fill_n generate generate_n transform for_each for_each_n find_if find_if_not find_end find_first_of adjacent_find count_if mismatch equal search search_n all_of any_of none_of is_sorted is_sorted_until binary_search merge inplace_merge includes set_union set_intersection set_difference set_symmetric_difference next_permutation prev_permutation min max minmax clamp min_element max_element minmax_element distance advance next prev iter_swap accumulate reduce transform_reduce inner_product partial_sum adjacent_difference iota gcd lcm midpoint "
      "unique_ptr shared_ptr weak_ptr make_unique make_shared allocate_shared static_pointer_cast dynamic_pointer_cast const_pointer_cast reinterpret_pointer_cast allocator allocator_traits enable_shared_from_this nullptr_t bad_alloc exception runtime_error logic_error invalid_argument out_of_range overflow_error underflow_error length_error domain_error current_exception rethrow_exception exception_ptr terminate "
      "thread jthread mutex recursive_mutex timed_mutex shared_mutex lock_guard unique_lock scoped_lock shared_lock condition_variable condition_variable_any atomic atomic_flag memory_order_relaxed memory_order_acquire memory_order_release memory_order_seq_cst promise future shared_future async launch packaged_task this_thread sleep_for sleep_until yield get_id join detach joinable hardware_concurrency "
      "chrono duration time_point system_clock steady_clock high_resolution_clock hours minutes seconds milliseconds microseconds nanoseconds duration_cast time_point_cast now filesystem path directory_iterator recursive_directory_iterator exists create_directory create_directories current_path file_size is_directory is_regular_file copy_file remove_all absolute canonical weakly_canonical relative "
      "regex smatch cmatch regex_match regex_search regex_replace random_device mt19937 mt19937_64 uniform_int_distribution uniform_real_distribution normal_distribution numeric_limits integral_constant true_type false_type enable_if conditional decay remove_reference remove_const is_same is_integral is_floating_point is_pointer is_class is_base_of is_convertible underlying_type void_t common_type "
      "ranges views filter take drop take_while drop_while split join transform reverse counted subrange iterator sentinel begin end input_iterator output_iterator forward_iterator bidirectional_iterator random_access_iterator contiguous_iterator "
      "iomanip setw setprecision setfill fixed scientific boolalpha noboolalpha hex oct dec showbase showpoint noshowpoint left right internal quoted buffer streambuf streamsize streampos seekg seekp tellg tellp read write good fail bad eof ignore peek putback sync_with_stdio";
    return words;
}
inline const std::unordered_map<std::string,std::string>& FunctionTips() {
    static const std::unordered_map<std::string,std::string> tips={
      {"printf","int printf(const char* format, ...)"},{"scanf","int scanf(const char* format, ...)"},
      {"fprintf","int fprintf(FILE* stream, const char* format, ...)"},{"snprintf","int snprintf(char* buffer, size_t size, const char* format, ...)"},
      {"puts","int puts(const char* text)"},{"putchar","int putchar(int character)"},{"getchar","int getchar(void)"},
      {"fopen","FILE* fopen(const char* filename, const char* mode)"},{"fclose","int fclose(FILE* stream)"},
      {"fgets","char* fgets(char* buffer, int size, FILE* stream)"},{"malloc","void* malloc(size_t bytes)"},
      {"calloc","void* calloc(size_t count, size_t size)"},{"realloc","void* realloc(void* memory, size_t bytes)"},
      {"free","void free(void* memory)"},{"strlen","size_t strlen(const char* text)"},
      {"strcmp","int strcmp(const char* left, const char* right)"},{"strcpy","char* strcpy(char* destination, const char* source)"},
      {"memcpy","void* memcpy(void* destination, const void* source, size_t bytes)"},{"memset","void* memset(void* memory, int value, size_t bytes)"},
      {"sort","std::sort(first, last [, compare])"},{"getline","std::getline(input_stream, string [, delimiter])"},
      {"pow","double pow(double base, double exponent)"},{"sqrt","double sqrt(double value)"},
      {"push_back","container.push_back(value)"},{"emplace_back","container.emplace_back(arguments...)"},
      {"resize","container.resize(count [, value])"},{"reserve","container.reserve(capacity)"},
      {"substr","string.substr(position = 0, count = npos)"},{"find","container.find(value)"},
      {"make_unique","std::make_unique<T>(constructor_arguments...)"},{"make_shared","std::make_shared<T>(constructor_arguments...)"},
      {"min","std::min(left, right)"},{"max","std::max(left, right)"},{"clamp","std::clamp(value, low, high)"},
      {"print","std::print(format_string, arguments...)  [C++23]"},{"println","std::println(format_string, arguments...)  [C++23]"}
    };return tips;
}
inline std::vector<std::string> Complete(const std::string& prefix,const std::string& document) {
    if(prefix.empty()) return {};
    std::set<std::string> matches;
    auto add=[&](const std::string& word){if(word.size()>prefix.size()&&word.compare(0,prefix.size(),prefix)==0)matches.insert(word);};
    std::istringstream catalog(CompletionWords());std::string word;while(catalog>>word)add(word);
    // Only identifiers are collected; this is local completion, not a semantic parser.
    word.clear();size_t limit=std::min<size_t>(document.size(),2*1024*1024);
    for(size_t i=0;i<limit;++i){unsigned char c=document[i];if(std::isalnum(c)||c=='_')word+=static_cast<char>(c);else{if(!word.empty()&&!std::isdigit(static_cast<unsigned char>(word[0])))add(word);word.clear();}}
    if(!word.empty()&&!std::isdigit(static_cast<unsigned char>(word[0])))add(word);
    std::vector<std::string> result(matches.begin(),matches.end());if(result.size()>80)result.resize(80);return result;
}
}
