#include "PathUtils.h"
#include "StringUtils.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <fnmatch.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <system_error>
#include <unistd.h>

namespace utils {
namespace fs = std::filesystem;
namespace {
constexpr std::uint32_t ErrorSuccess = 0;
constexpr std::uint32_t ErrorFileNotFound = 2;
constexpr std::uint32_t ErrorAccessDenied = 5;
constexpr std::uint32_t ErrorInvalidData = 13;

fs::path P(const std::wstring &value) { return fs::path(Utf16ToUtf8(value)); }
std::wstring W(const fs::path &value) { return Utf8ToUtf16(value.string()); }
std::uint32_t ErrorCode(const std::error_code &ec) {
    if (!ec) return ErrorSuccess;
    if (ec == std::errc::no_such_file_or_directory) return ErrorFileNotFound;
    if (ec == std::errc::permission_denied) return ErrorAccessDenied;
    return static_cast<std::uint32_t>(ec.value() ? ec.value() : ErrorInvalidData);
}
bool Match(const fs::path &path, const std::string &pattern) {
    return ::fnmatch(pattern.c_str(), path.filename().string().c_str(), FNM_CASEFOLD) == 0;
}
template <typename String>
String TrimSeparators(String path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.pop_back();
    return path;
}
}

#define UTF8_A_W_BOOL(Name) \
bool Name##A(const std::string &p) { return Name##Utf8(p); } \
bool Name##W(const std::wstring &p) { return Name##Utf8(Utf16ToUtf8(p)); }

bool FileExistsUtf8(const std::string &p) { std::error_code ec; return !p.empty() && fs::is_regular_file(fs::u8path(p), ec); }
UTF8_A_W_BOOL(FileExists)
bool DirectoryExistsUtf8(const std::string &p) { std::error_code ec; return !p.empty() && fs::is_directory(fs::u8path(p), ec); }
UTF8_A_W_BOOL(DirectoryExists)
bool PathExistsUtf8(const std::string &p) { std::error_code ec; return !p.empty() && fs::exists(fs::u8path(p), ec); }
UTF8_A_W_BOOL(PathExists)
bool CreateDirectoryUtf8(const std::string &p) { std::error_code ec; return !p.empty() && (fs::create_directory(fs::u8path(p), ec) || fs::is_directory(fs::u8path(p), ec)); }
UTF8_A_W_BOOL(CreateDirectory)
bool CreateFileTreeUtf8(const std::string &p) { std::error_code ec; return !p.empty() && (fs::create_directories(fs::u8path(p), ec) || fs::is_directory(fs::u8path(p), ec)); }
UTF8_A_W_BOOL(CreateFileTree)
bool DeleteFileUtf8(const std::string &p) { std::error_code ec; return fs::remove(fs::u8path(p), ec); }
UTF8_A_W_BOOL(DeleteFile)
bool DeleteDirectoryUtf8(const std::string &p) { std::error_code ec; return fs::remove_all(fs::u8path(p), ec) > 0 && !ec; }
UTF8_A_W_BOOL(DeleteDirectory)
bool CopyFileUtf8(const std::string &a, const std::string &b) { std::error_code ec; fs::create_directories(fs::u8path(b).parent_path(), ec); ec.clear(); return fs::copy_file(fs::u8path(a), fs::u8path(b), fs::copy_options::overwrite_existing, ec); }
bool CopyFileA(const std::string &a,const std::string &b){return CopyFileUtf8(a,b);} bool CopyFileW(const std::wstring&a,const std::wstring&b){return CopyFileUtf8(Utf16ToUtf8(a),Utf16ToUtf8(b));}
bool MoveFileUtf8(const std::string &a, const std::string &b) { std::error_code ec; fs::create_directories(fs::u8path(b).parent_path(), ec); ec.clear(); fs::rename(fs::u8path(a), fs::u8path(b), ec); return !ec; }
bool MoveFileA(const std::string&a,const std::string&b){return MoveFileUtf8(a,b);} bool MoveFileW(const std::wstring&a,const std::wstring&b){return MoveFileUtf8(Utf16ToUtf8(a),Utf16ToUtf8(b));}
bool ExtractZipUtf8(const std::string &, const std::string &) { return false; }
bool ExtractZipA(const std::string&a,const std::string&b){return ExtractZipUtf8(a,b);} bool ExtractZipW(const std::wstring&a,const std::wstring&b){return ExtractZipUtf8(Utf16ToUtf8(a),Utf16ToUtf8(b));}

std::string GetDriveA(const std::string &){return{};} std::wstring GetDriveW(const std::wstring&){return{};} std::string GetDriveUtf8(const std::string&){return{};}
std::string GetDirectoryUtf8(const std::string&p){return fs::u8path(p).parent_path().string();} std::string GetDirectoryA(const std::string&p){return GetDirectoryUtf8(p);} std::wstring GetDirectoryW(const std::wstring&p){return Utf8ToUtf16(GetDirectoryUtf8(Utf16ToUtf8(p)));}
std::pair<std::string,std::string> GetDriveAndDirectoryUtf8(const std::string&p){return{{},GetDirectoryUtf8(p)};} std::pair<std::string,std::string> GetDriveAndDirectoryA(const std::string&p){return GetDriveAndDirectoryUtf8(p);} std::pair<std::wstring,std::wstring> GetDriveAndDirectoryW(const std::wstring&p){return{{},GetDirectoryW(p)};}
std::string GetFileNameUtf8(const std::string&p){return fs::u8path(p).filename().string();} std::string GetFileNameA(const std::string&p){return GetFileNameUtf8(p);} std::wstring GetFileNameW(const std::wstring&p){return Utf8ToUtf16(GetFileNameUtf8(Utf16ToUtf8(p)));}
std::string GetExtensionUtf8(const std::string&p){return fs::u8path(p).extension().string();} std::string GetExtensionA(const std::string&p){return GetExtensionUtf8(p);} std::wstring GetExtensionW(const std::wstring&p){return Utf8ToUtf16(GetExtensionUtf8(Utf16ToUtf8(p)));}
std::string RemoveExtensionUtf8(const std::string&p){fs::path v=fs::u8path(p);v.replace_extension();return v.string();} std::string RemoveExtensionA(const std::string&p){return RemoveExtensionUtf8(p);} std::wstring RemoveExtensionW(const std::wstring&p){return Utf8ToUtf16(RemoveExtensionUtf8(Utf16ToUtf8(p)));}
std::string CombinePathUtf8(const std::string&a,const std::string&b){return (fs::u8path(a)/fs::u8path(b)).string();} std::string CombinePathA(const std::string&a,const std::string&b){return CombinePathUtf8(a,b);} std::wstring CombinePathW(const std::wstring&a,const std::wstring&b){return Utf8ToUtf16(CombinePathUtf8(Utf16ToUtf8(a),Utf16ToUtf8(b)));}
std::string NormalizePathUtf8(const std::string&p){return fs::u8path(p).lexically_normal().string();} std::string NormalizePathA(const std::string&p){return NormalizePathUtf8(p);} std::wstring NormalizePathW(const std::wstring&p){return Utf8ToUtf16(NormalizePathUtf8(Utf16ToUtf8(p)));}
std::wstring TrimTrailingSeparatorsW(const std::wstring&p){return TrimSeparators(p);} std::wstring GetParentDirectoryW(const std::wstring&p){return W(P(p).parent_path());}
bool IsPathInsideRootW(const std::wstring&p,const std::wstring&r){std::error_code ec;fs::path a=fs::weakly_canonical(P(p),ec);if(ec)return false;fs::path b=fs::weakly_canonical(P(r),ec);if(ec)return false;auto ai=a.begin(),bi=b.begin();for(;bi!=b.end();++bi,++ai)if(ai==a.end()||*ai!=*bi)return false;return true;}
bool TryGetFinalPathW(const std::wstring&p,std::wstring&o){std::error_code ec;auto v=fs::canonical(P(p),ec);if(ec)return false;o=W(v);return true;} bool TryGetFinalPathInsideRootW(const std::wstring&p,const std::wstring&r,std::wstring&o){return TryGetFinalPathW(p,o)&&IsPathInsideRootW(o,r);}
bool TryEncodePathForCodePage(std::wstring_view p,std::uint32_t cp,std::string&o){std::wstring w(p);o=Utf16ToUtf8(w);if(cp==20127)return std::all_of(o.begin(),o.end(),[](unsigned char c){return c<128;});return true;} bool TryEncodePathForActiveCodePage(std::wstring_view p,std::string&o){return TryEncodePathForCodePage(p,65001,o);} std::wstring GetShortPathW(const std::wstring&p){return p;}
bool IsPathValidUtf8(const std::string&p){return !p.empty()&&p.find('\0')==std::string::npos;} bool IsPathValidA(const std::string&p){return IsPathValidUtf8(p);} bool IsPathValidW(const std::wstring&p){return !p.empty()&&p.find(L'\0')==std::wstring::npos;}
bool IsAbsolutePathUtf8(const std::string&p){return fs::u8path(p).is_absolute();} bool IsAbsolutePathA(const std::string&p){return IsAbsolutePathUtf8(p);} bool IsAbsolutePathW(const std::wstring&p){return P(p).is_absolute();}
bool IsRelativePathUtf8(const std::string&p){return fs::u8path(p).is_relative();} bool IsRelativePathA(const std::string&p){return IsRelativePathUtf8(p);} bool IsRelativePathW(const std::wstring&p){return P(p).is_relative();}
bool IsPathRootedUtf8(const std::string&p){return IsAbsolutePathUtf8(p);} bool IsPathRootedA(const std::string&p){return IsPathRootedUtf8(p);} bool IsPathRootedW(const std::wstring&p){return IsAbsolutePathW(p);}
std::string ResolvePathUtf8(const std::string&p){std::error_code ec;auto v=fs::weakly_canonical(fs::absolute(fs::u8path(p),ec),ec);return ec?std::string{}:v.string();} std::string ResolvePathA(const std::string&p){return ResolvePathUtf8(p);} std::wstring ResolvePathW(const std::wstring&p){return Utf8ToUtf16(ResolvePathUtf8(Utf16ToUtf8(p)));}
std::string MakeRelativePathUtf8(const std::string&p,const std::string&b){std::error_code ec;auto v=fs::relative(fs::u8path(p),fs::u8path(b),ec);return ec?p:v.string();} std::string MakeRelativePathA(const std::string&p,const std::string&b){return MakeRelativePathUtf8(p,b);} std::wstring MakeRelativePathW(const std::wstring&p,const std::wstring&b){return Utf8ToUtf16(MakeRelativePathUtf8(Utf16ToUtf8(p),Utf16ToUtf8(b)));}

std::string GetTempPathUtf8(){std::error_code ec;return fs::temp_directory_path(ec).string();} std::string GetTempPathA(){return GetTempPathUtf8();} std::wstring GetTempPathW(){return Utf8ToUtf16(GetTempPathUtf8());}
std::string GetCurrentDirectoryUtf8(){std::error_code ec;return fs::current_path(ec).string();} std::string GetCurrentDirectoryA(){return GetCurrentDirectoryUtf8();} std::wstring GetCurrentDirectoryW(){return Utf8ToUtf16(GetCurrentDirectoryUtf8());}
bool SetCurrentDirectoryUtf8(const std::string&p){std::error_code ec;fs::current_path(fs::u8path(p),ec);return !ec;} bool SetCurrentDirectoryA(const std::string&p){return SetCurrentDirectoryUtf8(p);} bool SetCurrentDirectoryW(const std::wstring&p){return SetCurrentDirectoryUtf8(Utf16ToUtf8(p));}
std::string GetExecutablePathUtf8() {
#if defined(__APPLE__)
    uint32_t n = 0;
    _NSGetExecutablePath(nullptr, &n);
    std::string path(n, '\0');
    if (_NSGetExecutablePath(path.data(), &n) != 0) return {};
    path.resize(std::char_traits<char>::length(path.c_str()));
    return ResolvePathUtf8(path);
#elif defined(__EMSCRIPTEN__)
    // Virtual runtime layout. Mutable data uses BML's configured loader path.
    return "/Bin/Player";
#else
    std::vector<char> path(4096);
    const auto size = readlink("/proc/self/exe", path.data(), path.size());
    return size > 0 && static_cast<size_t>(size) < path.size()
        ? std::string(path.data(), static_cast<size_t>(size)) : std::string();
#endif
} std::string GetExecutablePathA(){return GetExecutablePathUtf8();} std::wstring GetExecutablePathW(){return Utf8ToUtf16(GetExecutablePathUtf8());}
int64_t GetFileSizeUtf8(const std::string&p){std::error_code ec;auto n=fs::file_size(fs::u8path(p),ec);return ec?-1:static_cast<int64_t>(n);} int64_t GetFileSizeA(const std::string&p){return GetFileSizeUtf8(p);} int64_t GetFileSizeW(const std::wstring&p){return GetFileSizeUtf8(Utf16ToUtf8(p));}
FileTime GetFileTimeUtf8(const std::string&p){std::error_code ec;auto t=fs::last_write_time(fs::u8path(p),ec);if(ec)return{};auto n=std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();return{n,n,n};} FileTime GetFileTimeA(const std::string&p){return GetFileTimeUtf8(p);} FileTime GetFileTimeW(const std::wstring&p){return GetFileTimeUtf8(Utf16ToUtf8(p));}

bool ReadFileBytesUtf8(const std::string&p,std::string&o){std::uint32_t e=0;return ReadFileBytesW(Utf8ToUtf16(p),o,e);} bool ReadFileBytesW(const std::wstring&p,std::string&o){std::uint32_t e=0;return ReadFileBytesW(p,o,e);} bool ReadFileBytesW(const std::wstring&p,std::string&o,std::uint32_t&e){o.clear();std::ifstream f(P(p),std::ios::binary);if(!f){e=errno==ENOENT?ErrorFileNotFound:ErrorAccessDenied;return false;}o.assign(std::istreambuf_iterator<char>(f),{});e=f.bad()?ErrorInvalidData:ErrorSuccess;return !f.bad();}
std::string ReadTextFileUtf8(const std::string&p){std::string o;ReadFileBytesUtf8(p,o);return o;} std::string ReadTextFileA(const std::string&p){return ReadTextFileUtf8(p);} std::wstring ReadTextFileW(const std::wstring&p){return Utf8ToUtf16(ReadTextFileUtf8(Utf16ToUtf8(p)));}
bool WriteTextFileUtf8(const std::string&p,const std::string&c){std::uint32_t e;return WriteFileBytesAtomicW(Utf8ToUtf16(p),c,e,true);} bool WriteTextFileA(const std::string&p,const std::string&c){return WriteTextFileUtf8(p,c);} bool WriteTextFileW(const std::wstring&p,const std::wstring&c){return WriteTextFileUtf8(Utf16ToUtf8(p),Utf16ToUtf8(c));}
std::vector<uint8_t> ReadBinaryFileUtf8(const std::string&p){std::string s;if(!ReadFileBytesUtf8(p,s))return{};return{reinterpret_cast<const uint8_t*>(s.data()),reinterpret_cast<const uint8_t*>(s.data()+s.size())};} std::vector<uint8_t> ReadBinaryFileA(const std::string&p){return ReadBinaryFileUtf8(p);} std::vector<uint8_t> ReadBinaryFileW(const std::wstring&p){return ReadBinaryFileUtf8(Utf16ToUtf8(p));}
bool WriteFileBytesAtomicW(const std::wstring&p,std::string_view d,std::uint32_t&e,bool replace){fs::path target=P(p);std::error_code ec;fs::create_directories(target.parent_path(),ec);if(!replace&&fs::exists(target,ec)){e=17;return false;}fs::path temp=target;temp+=std::string(".tmp.")+std::to_string(::getpid());{std::ofstream f(temp,std::ios::binary|std::ios::trunc);if(!f){e=ErrorAccessDenied;return false;}f.write(d.data(),static_cast<std::streamsize>(d.size()));f.flush();if(!f){fs::remove(temp,ec);e=ErrorInvalidData;return false;}}fs::rename(temp,target,ec);if(ec&&replace){fs::remove(target,ec);ec.clear();fs::rename(temp,target,ec);}e=ErrorCode(ec);return !ec;}
bool WriteBinaryFileUtf8(const std::string&p,const std::vector<uint8_t>&d){std::uint32_t e;return WriteFileBytesAtomicW(Utf8ToUtf16(p),std::string_view(reinterpret_cast<const char*>(d.data()),d.size()),e,true);} bool WriteBinaryFileA(const std::string&p,const std::vector<uint8_t>&d){return WriteBinaryFileUtf8(p,d);} bool WriteBinaryFileW(const std::wstring&p,const std::vector<uint8_t>&d){return WriteBinaryFileUtf8(Utf16ToUtf8(p),d);}
std::string CreateTempFileUtf8(const std::string&prefix){std::string t=CombinePathUtf8(GetTempPathUtf8(),(prefix.empty()?"bml":prefix)+".XXXXXX");std::vector<char>b(t.begin(),t.end());b.push_back('\0');int fd=mkstemp(b.data());if(fd<0)return{};close(fd);return b.data();} std::string CreateTempFileA(const std::string&p){return CreateTempFileUtf8(p);} std::wstring CreateTempFileW(const std::wstring&p){return Utf8ToUtf16(CreateTempFileUtf8(Utf16ToUtf8(p)));}

std::vector<std::string> ListFilesUtf8(const std::string&d,const std::string&pat){std::vector<std::string>o;std::error_code ec;for(fs::directory_iterator it(fs::u8path(d),ec),end;it!=end&&!ec;it.increment(ec))if(it->is_regular_file(ec)&&Match(it->path(),pat))o.push_back(it->path().filename().string());return o;} std::vector<std::string> ListFilesA(const std::string&d,const std::string&p){return ListFilesUtf8(d,p);} std::vector<std::wstring> ListFilesW(const std::wstring&d,const std::wstring&p){auto a=ListFilesUtf8(Utf16ToUtf8(d),Utf16ToUtf8(p));std::vector<std::wstring>o;for(auto&s:a)o.push_back(Utf8ToUtf16(s));return o;}
bool ListFilePathsRecursiveW(const std::wstring&d,std::vector<std::wstring>&o){o.clear();std::error_code ec;for(fs::recursive_directory_iterator it(P(d),ec),end;it!=end&&!ec;it.increment(ec))if(it->is_regular_file(ec))o.push_back(W(it->path()));return !ec;}
std::vector<std::string> ListDirectoriesUtf8(const std::string&d,const std::string&pat){std::vector<std::string>o;std::error_code ec;for(fs::directory_iterator it(fs::u8path(d),ec),end;it!=end&&!ec;it.increment(ec))if(it->is_directory(ec)&&Match(it->path(),pat))o.push_back(it->path().filename().string());return o;} std::vector<std::string> ListDirectoriesA(const std::string&d,const std::string&p){return ListDirectoriesUtf8(d,p);} std::vector<std::wstring> ListDirectoriesW(const std::wstring&d,const std::wstring&p){auto a=ListDirectoriesUtf8(Utf16ToUtf8(d),Utf16ToUtf8(p));std::vector<std::wstring>o;for(auto&s:a)o.push_back(Utf8ToUtf16(s));return o;}
}
