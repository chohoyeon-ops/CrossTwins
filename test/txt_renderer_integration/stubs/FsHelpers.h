#pragma once
#include <string>
namespace FsHelpers {
inline bool extension(const std::string& path,const char* suffix) { return path.size()>=4 && path.substr(path.size()-4)==suffix; }
inline bool hasTxtExtension(const std::string& p) { return extension(p,".txt"); }
inline bool hasMarkdownExtension(const std::string& p) { return extension(p,".md"); }
inline bool hasEpubExtension(const std::string& p) { return extension(p,".epub"); }
inline bool hasXtcExtension(const std::string& p) { return extension(p,".xtc") || extension(p,".xtch"); }
inline bool hasBmpExtension(const std::string& p) { return extension(p,".bmp"); }
inline bool hasJpgExtension(const std::string& p) { return extension(p,".jpg"); }
}
