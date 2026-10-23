#include "native_asset_hash.h"

#include <algorithm>
#include <bit>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace ff7rp::pipeline {
namespace {
using U = std::uint64_t;
constexpr U k0=0xc3a5c85c97cb3127ULL, k1=0xb492b66fbe98f273ULL, k2=0x9ae16a3b2f90404fULL;
U load(const std::uint8_t* p, unsigned n) { U v=0; for(unsigned i=0;i<n;++i) v|=U(p[i])<<(i*8); return v; }
U rot(U v,int s) { return std::rotr(v,s); }
U mix(U v) { return v^(v>>47); }
U h16(U u,U v,U mul=0x9ddfea08eb382d69ULL) { U a=mix((u^v)*mul); return mix((v^a)*mul)*mul; }
U swap(U x) { U y=0; for(int i=0;i<8;++i) { y=(y<<8)|(x&255); x>>=8; } return y; }
std::pair<U,U> weak(const std::uint8_t* p,U a,U b) {
    U w=load(p,8),x=load(p+8,8),y=load(p+16,8),z=load(p+24,8);
    a+=w; b=rot(b+a+z,21); U c=a; a+=x+y; b+=rot(a,44); return {a+z,b+c};
}
using Words=std::array<std::uint32_t,8>;
constexpr Words iv{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
struct BlakeOutput { Words cv; std::array<std::uint32_t,16> block; U counter; std::uint32_t len,flags; };
std::array<std::uint32_t,16> compress(const BlakeOutput& o, bool root=false) {
    std::array<std::uint32_t,16> v{},m=o.block;
    std::copy(o.cv.begin(),o.cv.end(),v.begin()); std::copy_n(iv.begin(),4,v.begin()+8);
    const U counter=root?0:o.counter;
    v[12]=std::uint32_t(counter); v[13]=std::uint32_t(counter>>32); v[14]=o.len; v[15]=o.flags|(root?8:0);
    auto g=[&](int a,int b,int c,int d,std::uint32_t x,std::uint32_t y) {
        v[a]+=v[b]+x; v[d]=std::rotr(v[d]^v[a],16); v[c]+=v[d]; v[b]=std::rotr(v[b]^v[c],12);
        v[a]+=v[b]+y; v[d]=std::rotr(v[d]^v[a],8); v[c]+=v[d]; v[b]=std::rotr(v[b]^v[c],7);
    };
    constexpr int perm[]{2,6,3,10,7,0,4,13,1,11,12,5,9,14,15,8};
    for(int r=0;r<7;++r) {
        g(0,4,8,12,m[0],m[1]); g(1,5,9,13,m[2],m[3]); g(2,6,10,14,m[4],m[5]); g(3,7,11,15,m[6],m[7]);
        g(0,5,10,15,m[8],m[9]); g(1,6,11,12,m[10],m[11]); g(2,7,8,13,m[12],m[13]); g(3,4,9,14,m[14],m[15]);
        auto old=m; for(int i=0;i<16;++i) m[i]=old[perm[i]];
    }
    for(int i=0;i<8;++i) { v[i]^=v[i+8]; v[i+8]^=o.cv[i]; } return v;
}
Words cv(const BlakeOutput& o) { auto v=compress(o); Words w{}; std::copy_n(v.begin(),8,w.begin()); return w; }
BlakeOutput tree(std::span<const std::uint8_t> b,U chunk) {
    if(b.size()>1024) {
        std::size_t left=1024; while(left*2<b.size()) left*=2;
        auto a=cv(tree(b.first(left),chunk)), c=cv(tree(b.subspan(left),chunk+left/1024));
        BlakeOutput o{iv,{},0,64,4}; std::copy(a.begin(),a.end(),o.block.begin()); std::copy(c.begin(),c.end(),o.block.begin()+8); return o;
    }
    Words chain=iv; BlakeOutput o{}; std::size_t at=0;
    do {
        const auto n=std::min<std::size_t>(64,b.size()-at);
        o={chain,{},chunk,std::uint32_t(n),std::uint32_t((at==0?1:0)|(at+n==b.size()?2:0))};
        for(std::size_t i=0;i<n;++i) o.block[i/4]|=std::uint32_t(b[at+i])<<((i%4)*8);
        chain=cv(o); at+=n;
    } while(at<b.size());
    return o;
}
}

std::uint64_t native_cityhash64(std::span<const std::uint8_t> b) {
    const auto n=b.size(); const auto p=b.data();
    if(n<=16) {
        if(n>=8) { U mul=k2+n*2,a=load(p,8)+k2,c=load(p+n-8,8); return h16(rot(c,37)*mul+a,(rot(a,25)+c)*mul,mul); }
        if(n>=4) return h16(n+(load(p,4)<<3),load(p+n-4,4),k2+n*2);
        if(n) return mix((U(p[0])+(U(p[n>>1])<<8))*k2^(n+(U(p[n-1])<<2))*k0)*k2;
        return k2;
    }
    if(n<=32) { U mul=k2+n*2,a=load(p,8)*k1,c=load(p+n-8,8)*mul,d=load(p+n-16,8)*k2,bb=load(p+8,8); return h16(rot(a+bb,43)+rot(c,30)+d,a+rot(bb+k2,18)+c,mul); }
    if(n<=64) {
        U mul=k2+n*2,a=load(p,8)*k2,bb=load(p+8,8),c=load(p+n-24,8),d=load(p+n-32,8);
        U e=load(p+16,8)*k2,f=load(p+24,8)*9,g=load(p+n-8,8),h=load(p+n-16,8)*mul;
        U u=rot(a+g,43)+(rot(bb,30)+c)*9,v=((a+g)^d)+f+1,w=swap((u+v)*mul)+h;
        U x=rot(e+f,42)+c,y=(swap((v+w)*mul)+g)*mul,z=e+f+c;
        a=swap((x+z)*mul+y)+bb; bb=mix((z+a)*mul+d+h)*mul; return bb+x;
    }
    U x=load(p+n-40,8),y=load(p+n-16,8)+load(p+n-56,8),z=h16(load(p+n-48,8)+n,load(p+n-24,8));
    auto v=weak(p+n-64,n,z),w=weak(p+n-32,y+k1,x); x=x*k1+load(p,8);
    for(std::size_t at=0,left=(n-1)&~std::size_t(63);left;at+=64,left-=64) {
        x=rot(x+y+v.first+load(p+at+8,8),37)*k1; y=rot(y+v.second+load(p+at+48,8),42)*k1;
        x^=w.second; y+=v.first+load(p+at+40,8); z=rot(z+w.first,33)*k1;
        v=weak(p+at,v.second*k1,x+w.first); w=weak(p+at+32,z+w.second,y+load(p+at+16,8)); std::swap(z,x);
    }
    return h16(h16(v.first,w.first)+mix(y)*k1+z,h16(v.second,w.second)+x);
}
std::uint64_t native_name_hash(std::string_view text,bool utf16) {
    std::vector<std::uint8_t> b; b.reserve(text.size()*(utf16?2:1));
    for(unsigned char c:text) { b.push_back(c>='A'&&c<='Z'?c+32:c); if(utf16) b.push_back(0); }
    return native_cityhash64(b);
}
std::array<std::uint8_t,32> native_blake3(std::span<const std::uint8_t> bytes) {
    auto v=compress(tree(bytes,0),true); std::array<std::uint8_t,32> out{};
    for(int i=0;i<32;++i) out[i]=std::uint8_t(v[i/4]>>((i%4)*8)); return out;
}
namespace {
std::array<std::uint8_t,32> portable_sha256(std::span<const std::uint8_t> bytes) {
    constexpr std::uint32_t k[]{0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    Words h=iv; const auto padded=(bytes.size()+9+63)&~std::size_t(63);
    for(std::size_t at=0;at<padded;at+=64) {
        std::uint32_t w[64]{};
        for(std::size_t j=0;j<64;++j) {
            auto pos=at+j; std::uint8_t c=pos<bytes.size()?bytes[pos]:(pos==bytes.size()?128:0);
            if(pos>=padded-8) c=std::uint8_t((U(bytes.size())*8)>>((padded-1-pos)*8));
            w[j/4]|=std::uint32_t(c)<<(24-(j%4)*8);
        }
        for(int i=16;i<64;++i) { auto a=w[i-15],b=w[i-2]; w[i]=w[i-16]+(std::rotr(a,7)^std::rotr(a,18)^(a>>3))+w[i-7]+(std::rotr(b,17)^std::rotr(b,19)^(b>>10)); }
        auto v=h;
        for(int i=0;i<64;++i) {
            auto t1=v[7]+(std::rotr(v[4],6)^std::rotr(v[4],11)^std::rotr(v[4],25))+((v[4]&v[5])^(~v[4]&v[6]))+k[i]+w[i];
            auto t2=(std::rotr(v[0],2)^std::rotr(v[0],13)^std::rotr(v[0],22))+((v[0]&v[1])^(v[0]&v[2])^(v[1]&v[2]));
            for(int j=7;j>0;--j) v[j]=v[j-1]; v[4]+=t1; v[0]=t1+t2;
        }
        for(int i=0;i<8;++i) h[i]+=v[i];
    }
    std::array<std::uint8_t,32> out{}; for(int i=0;i<32;++i) out[i]=std::uint8_t(h[i/4]>>(24-(i%4)*8)); return out;
}

#ifdef _WIN32
bool platform_sha256(std::span<const std::uint8_t> bytes, std::array<std::uint8_t,32>& out) {
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Hash() {
            if (value) BCryptDestroyHash(value);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } hash;
    if (BCryptOpenAlgorithmProvider(&hash.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(hash.algorithm, &hash.value, nullptr, 0, nullptr, 0, 0) < 0)
        return false;
    for (std::size_t at = 0; at < bytes.size();) {
        // Bounded chunks fit CNG's ULONG length even for a larger input span.
        const auto count = static_cast<ULONG>(std::min<std::size_t>(bytes.size() - at, 1024 * 1024));
        if (BCryptHashData(hash.value, const_cast<PUCHAR>(bytes.data() + at), count, 0) < 0)
            return false;
        at += count;
    }
    std::array<std::uint8_t,32> digest{};
    if (BCryptFinishHash(hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        return false;
    out = digest;
    return true;
}
#endif
} // namespace

std::array<std::uint8_t,32> native_sha256(std::span<const std::uint8_t> bytes) {
#ifdef _WIN32
    std::array<std::uint8_t,32> digest{};
    if (platform_sha256(bytes, digest)) return digest;
#endif
    return portable_sha256(bytes);
}
} // namespace ff7rp::pipeline
