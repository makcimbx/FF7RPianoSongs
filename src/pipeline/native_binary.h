#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ff7rp::pipeline::native_binary {
using Bytes = std::vector<std::uint8_t>;
inline bool range(std::span<const std::uint8_t> b, std::uint64_t at, std::uint64_t n) { return at<=b.size() && n<=b.size()-at; }
inline std::uint64_t get(std::span<const std::uint8_t> b,std::size_t at,unsigned n=4) { std::uint64_t v=0; for(unsigned i=0;i<n;++i) v|=std::uint64_t(b[at+i])<<(i*8); return v; }
inline void put(Bytes& b,std::size_t at,std::uint64_t v,unsigned n=4) { for(unsigned i=0;i<n;++i) b[at+i]=std::uint8_t(v>>(i*8)); }
inline void add(Bytes& b,std::uint64_t v,unsigned n=4) { auto at=b.size(); b.resize(at+n); put(b,at,v,n); }
inline void append(Bytes& b,std::span<const std::uint8_t> s) { b.insert(b.end(),s.begin(),s.end()); }
inline void string(Bytes& b,const std::string& s) { add(b,s.size()+1); b.insert(b.end(),s.begin(),s.end()); b.push_back(0); }
inline bool ascii(const std::string& s) { for(unsigned char c:s) if(c<32 || c>126) return false; return !s.empty(); }
} // namespace ff7rp::pipeline::native_binary
