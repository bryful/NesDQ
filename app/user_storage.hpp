#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <jni.h>
#include <codecvt>
#include <locale>
#endif
namespace userstore {
inline std::string utf8(const std::filesystem::path& p){auto s=p.u8string();return {s.begin(),s.end()};}
inline bool shared(const std::filesystem::path& p) { return p.generic_string().rfind("/nesdq-shared/",0)==0; }
#ifdef __ANDROID__
struct Bridge {
 JNIEnv* env=static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
 jobject activity=static_cast<jobject>(SDL_GetAndroidActivity());
 jclass type=env->GetObjectClass(activity);
 ~Bridge(){env->DeleteLocalRef(type);env->DeleteLocalRef(activity);}
 jstring path(const std::filesystem::path& p){std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> converter;auto s=converter.from_bytes(p.generic_string().substr(14));return env->NewString(reinterpret_cast<const jchar*>(s.data()),int(s.size()));}
 void check(){if(env->ExceptionCheck()){env->ExceptionDescribe();env->ExceptionClear();throw std::runtime_error("Shared NesDQ folder unavailable or file operation failed; check folder permission");}}
};
inline bool ready(){Bridge b;auto m=b.env->GetMethodID(b.type,"storageReady","()Z");bool r=b.env->CallBooleanMethod(b.activity,m);b.check();return r;}
#endif
inline void flush(){
#ifdef __ANDROID__
 Bridge b;auto m=b.env->GetMethodID(b.type,"storageFlush","()V");b.env->CallVoidMethod(b.activity,m);b.check();
#endif
}
inline void check(){
#ifdef __ANDROID__
 Bridge b;auto m=b.env->GetMethodID(b.type,"storageFailure","()Ljava/lang/String;");auto message=static_cast<jstring>(b.env->CallObjectMethod(b.activity,m));b.check();if(message){const char* s=b.env->GetStringUTFChars(message,nullptr);std::string error=s;b.env->ReleaseStringUTFChars(message,s);b.env->DeleteLocalRef(message);throw std::runtime_error(error);}
#endif
}
inline bool exists(const std::filesystem::path& p){
#ifdef __ANDROID__
 if(shared(p)){Bridge b;auto s=b.path(p);auto m=b.env->GetMethodID(b.type,"storageExists","(Ljava/lang/String;)Z");bool r=b.env->CallBooleanMethod(b.activity,m,s);b.env->DeleteLocalRef(s);b.check();return r;}
#endif
 auto backup=p;backup+=".bak";return std::filesystem::exists(p)||std::filesystem::exists(backup);
}
inline void mkdir(const std::filesystem::path& p){
#ifdef __ANDROID__
 if(shared(p)){Bridge b;auto s=b.path(p);auto m=b.env->GetMethodID(b.type,"storageMkdir","(Ljava/lang/String;)V");b.env->CallVoidMethod(b.activity,m,s);b.env->DeleteLocalRef(s);b.check();return;}
#endif
 std::filesystem::create_directories(p);
}
inline std::vector<uint8_t> read(const std::filesystem::path& p,size_t limit){
#ifdef __ANDROID__
 if(shared(p)){Bridge b;auto s=b.path(p);auto m=b.env->GetMethodID(b.type,"storageRead","(Ljava/lang/String;I)[B");auto a=static_cast<jbyteArray>(b.env->CallObjectMethod(b.activity,m,s,int(limit)));b.env->DeleteLocalRef(s);b.check();if(!a)throw std::runtime_error("Cannot read shared file");std::vector<uint8_t> r(b.env->GetArrayLength(a));b.env->GetByteArrayRegion(a,0,r.size(),reinterpret_cast<jbyte*>(r.data()));b.env->DeleteLocalRef(a);return r;}
#endif
 auto source=p;if(!std::filesystem::exists(source)){source+=".bak";}
 std::ifstream in(source,std::ios::binary|std::ios::ate);if(!in)throw std::runtime_error("Cannot open file");auto n=in.tellg();if(n<0||uint64_t(n)>limit)throw std::runtime_error("File too large");std::vector<uint8_t> r(static_cast<size_t>(n));in.seekg(0);if(!in.read(reinterpret_cast<char*>(r.data()),r.size()))throw std::runtime_error("Cannot read file");return r;
}
inline void write(const std::filesystem::path& p,const char* data,size_t size){
#ifdef __ANDROID__
 if(shared(p)){Bridge b;auto s=b.path(p);auto a=b.env->NewByteArray(size);b.env->SetByteArrayRegion(a,0,size,reinterpret_cast<const jbyte*>(data));auto m=b.env->GetMethodID(b.type,"storageWrite","(Ljava/lang/String;[B)V");b.env->CallVoidMethod(b.activity,m,s,a);b.env->DeleteLocalRef(s);b.env->DeleteLocalRef(a);b.check();return;}
#endif
 auto temp=p;temp+=".tmp";{std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(data,size);out.flush();if(!out)throw std::runtime_error("Cannot write file");}if(std::filesystem::exists(p)){auto backup=p;backup+=".bak";std::filesystem::copy_file(p,backup,std::filesystem::copy_options::overwrite_existing);}if(!SDL_RenamePath(userstore::utf8(temp).c_str(),userstore::utf8(p).c_str()))throw std::runtime_error(SDL_GetError());
}
inline void copy(const std::filesystem::path& a,const std::filesystem::path& b){if(userstore::exists(b))throw std::runtime_error("Backup already exists");auto data=read(a,32*1024*1024);write(b,reinterpret_cast<const char*>(data.data()),data.size());}
}
