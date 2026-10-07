// Original game execution and HD raster bridge. GPL-2.0-or-later.
// Game rules remain in the supplied ARM program; this file supplies local OS APIs.
#include <jni.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <unicorn/unicorn.h>
#include <unicorn/arm.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <set>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "offline_items.h"

namespace {
constexpr uint32_t BASE=0x100000, HEAP=0x10000000, STACK=0x20000000, HOST=0x30000000;
constexpr int SCALE=6, WIDTH=1440, HEIGHT=1920;
constexpr int regs[4]={UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3};
enum Kind {HELPER,DECOMPRESS,SHELL,DISPLAY,BITMAP,DIB,FILEMGR,FILEOBJ,OPTIONAL};
struct Object {Kind kind; int refs=1; std::vector<uint8_t> data; size_t cursor=0; std::string name; bool writable=false;};
struct Callback {Kind kind; uint32_t offset;};
struct Timer {uint32_t due,func,user;};
struct Texture {int width,height; std::vector<uint32_t> pixels;};

class Runtime {
 public:
  uc_engine* uc=nullptr;
  AAssetManager* assets;
  std::string saveDir,error,lastBlit;
  uint32_t host=HOST+0x100,helper,shell,context,display,bitmap,dib,framebuffer,appOut=0,app=0,handler=0;
  uint32_t clockMs=1000,frames=0,randomState=0xabc123;
  uint64_t hdBlits=0,hdGlyphs=0;
  uint32_t fillTint=0xff000000;
  std::map<uint32_t,uint32_t> freeBlocks,allocated;
  std::unordered_map<uint32_t,Callback> callbacks;
  std::map<Kind,uint32_t> vtables;
  std::unordered_map<uint32_t,Object> objects;
  std::vector<Timer> timers;
  std::unordered_map<uint64_t,Texture> textures;
  std::unordered_map<uint32_t,uint64_t> bindings;
  std::unordered_map<uint64_t,bool> verifiedImages;
  std::unordered_map<uint16_t,size_t> glyphOffsets;
  std::vector<uint8_t> glyphs;
  std::vector<uint32_t> canvas;
  bool booted=false,rendering=true,hdSprite=false;
  uint32_t spriteUntil=0;
  uint32_t pendingOfflineSave=0;
  struct Transform {float ox=0,oy=0,nx=0,ny=0,scale=1;bool active=false;} transform;
  struct Draw {uint64_t key;int x,y,w,h,sx,sy;uint32_t flags,tint;int opacity,mix;};
  std::vector<Draw> effectDraws;
  bool collectingEffects=false,battleBackdropClear=false;
  std::unordered_map<uint32_t,std::pair<float,float>> unitOrigins;
  std::unordered_map<uint32_t,int32_t> unitHealth;
  std::set<uint32_t> previousEffects,currentEffects;
  uint32_t soundEvents=0,lastSoundScene=0,lastStage=0,lastStep=0;
  int oldHeroX=0,oldHeroY=0;
  uint64_t battleUnitDraws=0,battleEffectGroups=0,battleCommandDraws=0;
  bool victoryPlayed=false,bossMusic=false,quitRequested=false;
  uint16_t selectedBattleCommand=0;

  Runtime(AAssetManager* manager,std::string directory):assets(manager),saveDir(std::move(directory)),canvas(WIDTH*HEIGHT,0xff000000) {
    check(uc_open(UC_ARCH_ARM,UC_MODE_ARM,&uc));
    check(uc_mem_map(uc,BASE,0x100000,UC_PROT_ALL));
    check(uc_mem_map(uc,HEAP,0x4000000,UC_PROT_ALL));
    check(uc_mem_map(uc,STACK,0x100000,UC_PROT_ALL));
    check(uc_mem_map(uc,HOST,0x100000,UC_PROT_ALL));
    freeBlocks[HEAP]=0x4000000;
    auto packed=asset("original/bin.dat");
    if(packed.size()<4) throw std::runtime_error("Original program asset missing");
    auto program=inflateBytes(packed.data()+4,packed.size()-4);
    if(program.size()!=158224) throw std::runtime_error("Unexpected original ARM program size");
    write(BASE,program.data(),program.size());
    w32(HOST,0xe12fff1e);
    uc_hook hook;
    check(uc_hook_add(uc,&hook,UC_HOOK_CODE,(void*)dispatchThunk,this,HOST,HOST+0xfffff));
    helper=alloc(0x400);
    for(uint32_t n=0;n<0x400;n+=4)w32(helper+n,callback(HELPER,n));
    shell=interface(SHELL); context=alloc(64); w32(context+12,shell);
    display=interface(DISPLAY);bitmap=interface(BITMAP);dib=interface(DIB);
    framebuffer=alloc(240*320*2);w32(dib+8,framebuffer);
    int16_t layout[3]={240,320,480};write(dib+20,layout,sizeof(layout));uint8_t depth=16;write(dib+29,&depth,1);
    w32(BASE,callback(DECOMPRESS,0));w32(BASE+4,0);w32(BASE+8,helper);
    check(uc_hook_add(uc,&hook,UC_HOOK_CODE,(void*)registerThunk,this,BASE+0xc440,BASE+0xc440));
    check(uc_hook_add(uc,&hook,UC_HOOK_CODE,(void*)blitThunk,this,BASE+0x278c,BASE+0x278c));
    check(uc_hook_add(uc,&hook,UC_HOOK_CODE,(void*)glyphThunk,this,BASE+0x18108,BASE+0x18108));
    check(uc_hook_add(uc,&hook,UC_HOOK_CODE,(void*)uiBackgroundThunk,this,BASE+0xb4ec,BASE+0xb4ec));
    for(uint32_t offset:{0x17004u,0x22060u,0x1a9d4u,0x1aa10u,0x1ab24u,0x10c10u,0x10c98u,0x10cb8u,0x1372cu})
      check(uc_hook_add(uc,&hook,UC_HOOK_CODE,(void*)offlineItemThunk,this,BASE+offset,BASE+offset));
    for(uint32_t offset:{0x9068u,0x93d4u,0x19134u,0x1941cu,0x18c84u,0x13164u})
      check(uc_hook_add(uc,&hook,UC_HOOK_CODE,(void*)presentationThunk,this,BASE+offset,BASE+offset));
    check(uc_hook_add(uc,&hook,UC_HOOK_MEM_WRITE,(void*)pixelThunk,this,framebuffer,framebuffer+240*320*2-1));
    check(uc_hook_add(uc,&hook,UC_HOOK_MEM_INVALID,(void*)invalidMemoryThunk,this,1,0));
  }
  ~Runtime(){if(uc)uc_close(uc);}
  void check(uc_err e){if(e!=UC_ERR_OK)throw std::runtime_error(uc_strerror(e));}
  void read(uint32_t a,void* p,size_t n){if(n)check(uc_mem_read(uc,a,p,n));}
  void write(uint32_t a,const void* p,size_t n){if(n)check(uc_mem_write(uc,a,p,n));}
  uint32_t r32(uint32_t a){uint32_t v;read(a,&v,4);return v;}
  uint16_t r16(uint32_t a){uint16_t v;read(a,&v,2);return v;}
  void w32(uint32_t a,uint32_t v){write(a,&v,4);}
  uint32_t reg(int r){uint32_t v;check(uc_reg_read(uc,r,&v));return v;}
  void setreg(int r,uint32_t v){check(uc_reg_write(uc,r,&v));}
  std::string string(uint32_t a){std::string out;for(int n=0;n<1024;n++){char c;read(a+n,&c,1);if(!c)return out;out+=c;}throw std::runtime_error("Unterminated resource path");}
  std::vector<uint8_t> asset(const std::string& name){
    AAsset* a=AAssetManager_open(assets,name.c_str(),AASSET_MODE_BUFFER);
    if(!a)return {};std::vector<uint8_t> data(AAsset_getLength(a));
    if(!data.empty())AAsset_read(a,data.data(),data.size());AAsset_close(a);return data;
  }
  std::vector<uint8_t> inflateBytes(const uint8_t* p,size_t n){
    z_stream z{}; if(inflateInit2(&z,31)!=Z_OK)throw std::runtime_error("gzip init failed");
    z.next_in=const_cast<uint8_t*>(p);z.avail_in=n;
    std::vector<uint8_t> out;int result=Z_OK;
    while(result==Z_OK){size_t start=out.size();out.resize(start+65536);z.next_out=out.data()+start;z.avail_out=65536;result=inflate(&z,Z_NO_FLUSH);out.resize(start+65536-z.avail_out);if(out.size()>0x1000000){inflateEnd(&z);throw std::runtime_error("Oversized original gzip");}}
    inflateEnd(&z);if(result!=Z_STREAM_END)throw std::runtime_error("Original gzip decode failed");return out;
  }
  uint32_t alloc(uint32_t n){
    if(n>0x1000000)throw std::runtime_error("Original allocation too large");
    n=std::max(16u,(n+15)&~15u);
    for(auto it=freeBlocks.begin();it!=freeBlocks.end();++it)if(it->second>=n){uint32_t a=it->first,left=it->second-n;freeBlocks.erase(it);if(left)freeBlocks[a+n]=left;allocated[a]=n;std::vector<uint8_t> zero(n);write(a,zero.data(),n);return a;}
    throw std::runtime_error("Original heap exhausted");
  }
  void releaseMemory(uint32_t a){
    if(!a)return;auto it=allocated.find(a);if(it==allocated.end())throw std::runtime_error("Original freed an unknown allocation");
    uint32_t n=it->second;allocated.erase(it);auto next=freeBlocks.lower_bound(a);
    if(next!=freeBlocks.end()&&a+n==next->first){n+=next->second;freeBlocks.erase(next);}next=freeBlocks.lower_bound(a);
    if(next!=freeBlocks.begin()){auto prev=std::prev(next);if(prev->first+prev->second==a){a=prev->first;n+=prev->second;freeBlocks.erase(prev);}}
    freeBlocks[a]=n;
  }
  uint32_t callback(Kind kind,uint32_t off){uint32_t a=host;host+=4;if(host>=HOST+0x100000)throw std::runtime_error("Host callback table exhausted");callbacks[a]={kind,off};w32(a,0xe12fff1e);return a;}
  uint32_t interface(Kind kind){
    uint32_t table;
    auto existing=vtables.find(kind);
    if(existing==vtables.end()){table=alloc(0x300);vtables[kind]=table;for(uint32_t n=0;n<0x300;n+=4)w32(table+n,callback(kind,n));}else table=existing->second;
    uint32_t a=alloc(128);w32(a,table);Object obj;obj.kind=kind;objects.emplace(a,std::move(obj));return a;
  }
  bool validName(const std::string& n){return !n.empty()&&n.find("..") == std::string::npos&&n.find('/')==std::string::npos&&n.find('\\')==std::string::npos&&n.find(':')==std::string::npos;}
  std::vector<uint8_t> gameFile(const std::string& name){
    if(!validName(name))throw std::runtime_error("Resource path outside original game mount");
    std::ifstream f(saveDir+"/"+name,std::ios::binary);if(f)return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
    return asset("original/"+name);
  }
  bool exists(const std::string& name){if(!validName(name))return false;std::ifstream f(saveDir+"/"+name);if(f)return true;AAsset* a=AAssetManager_open(assets,("original/"+name).c_str(),AASSET_MODE_BUFFER);if(a){AAsset_close(a);return true;}return false;}
  void flush(Object& o){
    if(!o.writable)return;
    if(o.name!="save.dat")throw std::runtime_error("Unexpected original write target: "+o.name);
    std::string temp=saveDir+"/save.dat.tmp";std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.write((const char*)o.data.data(),o.data.size());f.close();if(!f||std::rename(temp.c_str(),(saveDir+"/save.dat").c_str()))throw std::runtime_error("Private save could not be written");
  }
  uint32_t service(Kind kind,uint32_t off,const std::array<uint32_t,4>& arg){
    uint32_t a=arg[0],b=arg[1],c=arg[2],d=arg[3];
    if(kind==HELPER){
      if(off==0x68)return alloc(a);
      if(off==0x6c){releaseMemory(a);return 0;}
      if(off==0){std::vector<uint8_t> v(c);read(b,v.data(),c);write(a,v.data(),c);return a;}
      if(off==4){std::vector<uint8_t> v(c,b&255);write(a,v.data(),c);return a;}
      if(off==8){auto s=string(b);write(a,s.c_str(),s.size()+1);return a;}
      if(off==0x24){uint32_t n=0;while(n<32768&&r16(b+n))n+=2;if(n>=32768)throw std::runtime_error("Wide string not terminated");std::vector<uint8_t> v(n+2);read(b,v.data(),v.size());write(a,v.data(),v.size());return a;}
      if(off==0xc0)return (appOut?r32(appOut):0)?r32(appOut):context;
      if(off==0xc8){auto s=string(b);if(c){size_t n=std::min(s.size(),size_t(c-1));write(a,s.data(),n);uint8_t z=0;write(a+n,&z,1);}return s.size();}
      if(off==0xac)return uint32_t(std::time(nullptr)-315964800);
      if(off==0xb0)return clockMs;
      if(off==0xa8){std::vector<uint8_t> v(b);for(auto& x:v){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;x=randomState&255;}write(a,v.data(),v.size());return 0;}
    }
    if(kind==DECOMPRESS){
      std::vector<uint8_t> v(b);read(a,v.data(),b);auto raw=inflateBytes(v.data(),v.size());
      uint32_t dst=alloc(raw.size());write(dst,raw.data(),raw.size());
      // The original HKM loader locates the last image header using this length.
      if(c)w32(c,raw.size());return dst;
    }
    if(off==0||off==4){
      auto it=objects.find(a);if(it==objects.end())throw std::runtime_error("Invalid original interface object");
      if(off==0)return ++it->second.refs;
      if(it->second.refs>1)return --it->second.refs;
      if(kind==FILEOBJ||kind==FILEMGR||kind==OPTIONAL){flush(it->second);objects.erase(it);releaseMemory(a);return 0;}
      return 1;
    }
    if(kind==SHELL){
      if(off==0x18){quitRequested=true;__android_log_print(ANDROID_LOG_INFO,"XuanyuanOriginal","original exit requested");return 0;}
      if(off==0x2c){timers.push_back({clockMs+b,c,d});return 0;}
      if(off==0x30){timers.erase(std::remove_if(timers.begin(),timers.end(),[&](const Timer&t){return (!b||t.func==b)&&t.user==c;}),timers.end());return 0;}
      if(off==8){if(b==0x1001007){w32(c,0);return 3;}uint32_t obj=0;if(b==0x1001001)obj=display;else if(b==0x1001003)obj=interface(FILEMGR);else if(b==0x1005001)obj=interface(OPTIONAL);else throw std::runtime_error("Unknown original class "+std::to_string(b));w32(c,obj);return 0;}
      if(off==0x10){std::array<uint8_t,44> info{};int16_t wh[2]={240,320};std::memcpy(info.data(),wh,4);write(b,info.data(),info.size());return 0;}
      if(off==0x0c||off==0xb4)return 0;
    }
    if(kind==DISPLAY){
      if(off==0x3c)return bitmap;
      if(off==8){if(c)w32(c,12);if(d)w32(d,4);return 16;}
      if(off==0x1c){frames++;audioState();previousEffects=currentEffects;currentEffects.clear();return 0;}
      if(off==0x14){int16_t rect[4]={0,0,240,320};if(b)read(b,rect,8);uint32_t flags=r32(reg(UC_ARM_REG_SP));if(flags&2){uint16_t pixel=((d>>27)<<11)|(((d>>18)&63)<<5)|((d>>11)&31);for(int y=std::max(0,int(rect[1]));y<std::min(320,int(rect[1])+rect[3]);y++)for(int x=std::max(0,int(rect[0]));x<std::min(240,int(rect[0])+rect[2]);x++){write(framebuffer+(y*240+x)*2,&pixel,2);blockPixel(x,y,rgb565(pixel));}}return 0;}
    }
    if(kind==BITMAP&&off==8&&b==0x1001045){w32(c,dib);return 0;}
    if(kind==FILEMGR&&(off==8||off==12||off==28)){
      auto name=string(b);if(!validName(name))throw std::runtime_error("Invalid game file path");
      if(off==28)return exists(name)?0:1;
      if(off==12){if(!exists(name))return 1;uint32_t info[4]={0,0,uint32_t(gameFile(name).size()),0};write(c,info,16);return 0;}
      // BREW modes: READ=1, READWRITE=2, CREATE=4, APPEND=8.
      bool writing=(c&14)!=0;
      if(!exists(name)&&!writing)return 0;
      if(writing&&name!="save.dat")throw std::runtime_error("Unexpected original write file: "+name);
      uint32_t obj=interface(FILEOBJ);auto& file=objects.at(obj);file.name=name;file.writable=writing;file.data=(c&4)?std::vector<uint8_t>{}:gameFile(name);if(c&8)file.cursor=file.data.size();return obj;
    }
    if(kind==FILEOBJ){
      auto& o=objects.at(a);
      if(off==12){size_t n=std::min(size_t(c),o.cursor<o.data.size()?o.data.size()-o.cursor:0);write(b,o.data.data()+std::min(o.cursor,o.data.size()),n);o.cursor+=n;return n;}
      if(off==20){if(!o.writable)throw std::runtime_error("Write to read-only resource");if(o.cursor+c>0x1000000)throw std::runtime_error("Save size exceeds limit");o.data.resize(std::max(o.data.size(),o.cursor+c));read(b,o.data.data()+o.cursor,c);o.cursor+=c;flush(o);return c;}
      if(off==24){uint32_t info[4]={0,0,uint32_t(o.data.size()),0};write(b,info,16);return 0;}
      if(off==28){int64_t base=b==0?0:b==1?o.data.size():b==2?o.cursor:-1;int64_t p=base+int32_t(c);if(base<0||p<0||p>0x1000000)throw std::runtime_error("Invalid original file seek");o.cursor=p;return b==0?0:o.cursor;}
      if(off==32)return o.cursor;
      if(off==36){if(!o.writable)return 1;o.data.resize(b);o.cursor=std::min(o.cursor,o.data.size());flush(o);return 0;}
    }
    char msg[160];std::snprintf(msg,sizeof(msg),"Unsupported original OS service kind=%d offset=0x%x caller=0x%x",kind,off,reg(UC_ARM_REG_LR)-BASE);throw std::runtime_error(msg);
  }
  void fail(const std::string& message){if(error.empty()){error=message;__android_log_print(ANDROID_LOG_ERROR,"XuanyuanOriginal","%s",error.c_str());std::ofstream report(saveDir+"/compatibility-error.txt");report<<error<<"\n"<<lastBlit<<"\n";}uc_emu_stop(uc);}
  static bool invalidMemoryThunk(uc_engine*,uc_mem_type type,uint64_t address,int size,int64_t,void* user){auto*r=(Runtime*)user;char message[240];std::snprintf(message,sizeof(message),"Invalid original memory access type=%d address=0x%llx bytes=%d at ARM +0x%x r0=0x%x r1=0x%x lr=0x%x",type,(unsigned long long)address,size,r->reg(UC_ARM_REG_PC)-BASE,r->reg(UC_ARM_REG_R0),r->reg(UC_ARM_REG_R1),r->reg(UC_ARM_REG_LR));r->fail(message);return false;}
  static void dispatchThunk(uc_engine*,uint64_t address,uint32_t,void* user){auto* r=(Runtime*)user;try{
    if(address==HOST){uc_emu_stop(r->uc);return;}
    auto cb=r->callbacks.at(address);std::array<uint32_t,4> args;for(int i=0;i<4;i++)args[i]=r->reg(regs[i]);uint32_t value=r->service(cb.kind,cb.offset,args);r->setreg(UC_ARM_REG_R0,value);r->setreg(UC_ARM_REG_PC,r->reg(UC_ARM_REG_LR));
  }catch(const std::exception& e){r->fail(e.what());}}
  uint32_t call(uint32_t address,std::initializer_list<uint32_t> args){
    if(!error.empty())return 0;setreg(UC_ARM_REG_SP,STACK+0x80000);setreg(UC_ARM_REG_LR,HOST);int i=0;for(uint32_t value:args)setreg(regs[i++],value);
    // Full-screen fades and outdoor menus exceed the old five-million limit.
    uc_err code=uc_emu_start(uc,address,0,0,20000000);
    if(code!=UC_ERR_OK){char msg[180];std::snprintf(msg,sizeof(msg),"%s at original ARM +0x%x",uc_strerror(code),reg(UC_ARM_REG_PC)-BASE);fail(msg);}else if(error.empty()&&reg(UC_ARM_REG_PC)!=HOST){char msg[120];std::snprintf(msg,sizeof(msg),"Original instruction budget exhausted at ARM +0x%x",reg(UC_ARM_REG_PC)-BASE);fail(msg);}
    uint32_t result=reg(UC_ARM_REG_R0);
    if(error.empty()&&pendingOfflineSave){
      // Run only after the original input/timer handler has returned. Calling
      // ARM from inside a Unicorn hook would corrupt the suspended CPU stack.
      uint32_t data=pendingOfflineSave;pendingOfflineSave=0;
      if(call(BASE+0x1fcd8,{data})!=1&&error.empty())fail("Local free reward could not be saved");
      if(error.empty())__android_log_print(ANDROID_LOG_INFO,"XuanyuanOffline","original full reward save completed");
    }
    return result;
  }
  void boot(){
    uint32_t moduleOut=alloc(4),name=alloc(32);const char n[]="xuanyuan.mod";write(name,n,sizeof(n));
    if(call(BASE+12,{shell,name,moduleOut})!=0||!error.empty())throw std::runtime_error("Original module initialization: "+error);
    uint32_t module=r32(moduleOut);appOut=alloc(4);
    if(call(r32(r32(module)+8),{module,shell,0,appOut})!=0||!error.empty())throw std::runtime_error("Original app initialization: "+error);
    app=r32(appOut);handler=r32(app+0x18);call(handler,{app,0,0,0});booted=error.empty();
    __android_log_print(ANDROID_LOG_INFO,"XuanyuanOriginal","original ARM booted=%d viewport=%dx%d",booted,WIDTH,HEIGHT);
  }
  void tick(uint32_t elapsed){
    // Opt-in isolated development fixture, absent in normal installations.
    // It runs the original encounter initializer and never writes the save.
    if(app&&r32(app+0x2980)==5){std::ifstream f(saveDir+"/battle-test.flag");int encounter=0;if(f>>encounter){uint32_t view=r32(app+0x2984);if(encounter>=0&&encounter<10&&view&&r16(view+0x1d8)%256==0){f.close();std::remove((saveDir+"/battle-test.flag").c_str());call(BASE+0x309c,{view,11,uint32_t(encounter)});}}}
    clockMs+=elapsed;int budget=5;while(error.empty()&&budget--){auto next=std::min_element(timers.begin(),timers.end(),[](const Timer&a,const Timer&b){return a.due<b.due;});if(next==timers.end()||next->due>clockMs)break;Timer t=*next;timers.erase(next);call(t.func,{t.user});}}
  void key(uint32_t code,bool pressed){
    if(pressed&&app&&r32(app+0x2980)==5){
      uint32_t v=r32(app+0x2984),b=battle();
      if((code==0xe035&&b&&(r16(b+0x88)&255)==1&&selectedBattleCommand==25910)||
         (code==0xe02a&&!b&&v&&(r16(v+0x1d8)&255)==0))soundEvents|=1u<<9;
    }
    if(booted)call(handler,{app,pressed?0x101u:0x102u,code,0});
  }
  void captureRequested(){
    std::ifstream flag(saveDir+"/capture.flag");if(!flag)return;flag.close();
    std::vector<uint8_t> raw(240*320*2);read(framebuffer,raw.data(),raw.size());
    std::ofstream logical(saveDir+"/original-frame.rgb565",std::ios::binary);logical.write((const char*)raw.data(),raw.size());logical.close();
    std::ofstream hd(saveDir+"/hd-frame.argb",std::ios::binary);hd.write((const char*)canvas.data(),canvas.size()*4);hd.close();
    std::remove((saveDir+"/capture.flag").c_str());
  }
  static uint32_t rgb565(uint16_t p){return 0xff000000|((p>>11)*255/31<<16)|(((p>>5)&63)*255/63<<8)|(p&31)*255/31;}
  void pixelRect(int x,int y,uint32_t color,int alpha){
    if(!rendering||x<0||x>=240||y<0||y>=320)return;
    float scale=transform.active?transform.scale:1;
    float dx=transform.active?transform.nx+(x-transform.ox)*scale:x;
    float dy=transform.active?transform.ny+(y-transform.oy)*scale:y;
    int l=std::max(0,int(std::floor(dx*SCALE))),t=std::max(0,int(std::floor(dy*SCALE)));
    int right=std::min(WIDTH,int(std::floor((dx+scale)*SCALE))),bottom=std::min(HEIGHT,int(std::floor((dy+scale)*SCALE)));
    for(int yy=t;yy<bottom;yy++)for(int xx=l;xx<right;xx++)canvas[yy*WIDTH+xx]=blend(canvas[yy*WIDTH+xx],color,alpha);
  }
  void blockPixel(int x,int y,uint32_t color){pixelRect(x,y,color,255);}
  void blendPixel(int x,int y,uint32_t color,int alpha){pixelRect(x,y,color,alpha);}
  static void pixelThunk(uc_engine*,uc_mem_type,uint64_t address,int size,int64_t value,void* user){auto*r=(Runtime*)user;if(!r->rendering)return;uint32_t pc=r->reg(UC_ARM_REG_PC)-BASE;
    if(r->battleBackdropClear&&pc>=0xb4ec&&pc<0xb930)return;
    if((pc>=0x278c&&pc<=0x2d18&&r->hdSprite)||(pc>=0x18108&&pc<=0x186f4))return;
    for(int i=0;i<size;i+=2){size_t pos=(address+i-r->framebuffer)/2;uint16_t p=uint64_t(value)>>(i*8);
      if(p==0x40d4&&r->inBattle())continue;
      // The original translucent fill operates on its RGB565 framebuffer.
      // Apply the same tint/opacity to the HD destination, preserving detail.
      if(pc==0xb8d4){int retained=std::min(32u,r->reg(UC_ARM_REG_R5));r->blendPixel(pos%240,pos/240,r->fillTint,(32-retained)*255/32);}
      else if(pc==0xb84c)r->blendPixel(pos%240,pos/240,r->fillTint,128);
      else if(pc==0xb348||pc==0xb360||pc==0xb37c){uint16_t half=r->reg(UC_ARM_REG_R1)&0x7bef;r->blendPixel(pos%240,pos/240,rgb565(half<<1),128);}
      else r->blockPixel(pos%240,pos/240,rgb565(p));}}
  static uint64_t textureKey(const std::string& pack,uint32_t id){uint64_t p=pack=="map.hkm"?0:pack=="mapb.hkm"?1:pack=="mapc.hkm"?2:3;return (p<<32)|id;}
  static void registerThunk(uc_engine*,uint64_t,uint32_t,void* user){auto*r=(Runtime*)user;try{uint32_t m=r->reg(regs[0]),i=r->reg(regs[1]);uint32_t id=r->r32(r->r32(m+0x10)+i*4);uint64_t key=textureKey(r->string(m+0x18),id);r->bindings[r->r32(m+0x14)+i*32]=key;auto t=r->textures.find(key);if(t!=r->textures.end()){uint32_t header=r->reg(regs[2]);if(r->r16(header)!=t->second.width/SCALE||r->r16(header+2)!=t->second.height/SCALE)throw std::runtime_error("Original decoded image size mismatch for resource "+std::to_string(id));r->verifiedImages[key]=true;}}catch(const std::exception&e){r->fail(e.what());}}
  static void blitThunk(uc_engine*,uint64_t,uint32_t,void* user){auto*r=(Runtime*)user;try{r->blit();}catch(const std::exception&e){r->fail(e.what());}}
  static void offlineItemThunk(uc_engine*,uint64_t address,uint32_t,void* user){auto*r=(Runtime*)user;try{
    uint32_t offset=address-BASE;
    if(offset==0x17004){
      auto found=offlineItemTexts().find(r->reg(regs[1]));
      if(found==offlineItemTexts().end())return;
      const auto& text=found->second;uint32_t bytes=(text.size()+1)*2,dest=r->reg(regs[2]);
      // Match the original resource-loader contract, including length queries
      // and insufficient output buffers. Never truncate a UTF-16 string.
      if(dest&&r->reg(regs[3])>=bytes)r->write(dest,text.c_str(),bytes);
      r->setreg(regs[0],bytes);r->setreg(UC_ARM_REG_PC,r->reg(UC_ARM_REG_LR));return;
    }
    if(offset==0x22060){
      if(r->reg(UC_ARM_REG_LR)!=BASE+0x7054)return;
      // Only the retired billing list's price column; ordinary game money
      // and quantities continue through the original formatter.
      const char16_t label[]=u"免费";uint32_t dest=r->reg(regs[2]);
      if(dest)r->write(dest,label,sizeof(label));
      r->setreg(regs[0],2);r->setreg(UC_ARM_REG_PC,r->reg(UC_ARM_REG_LR));return;
    }
    if(offset==0x1a9d4){
      // Center the new free label, which no longer has a price to its right.
      r->setreg(UC_ARM_REG_R12,(r->reg(regs[1])-r->reg(regs[0]))/2);
      r->setreg(UC_ARM_REG_PC,BASE+0x1a9d8);return;
    }
    if(offset==0x1aa10){r->setreg(UC_ARM_REG_PC,BASE+0x1aad4);return;}
    if(offset==0x1372c){
      // Original billing arrays have a 12-byte stride although the recovered
      // table has 13 entries: item 12's count aliases item 0's SMS progress.
      // Its permanent entitlement is the original saved story/reward flag.
      if(r->reg(UC_ARM_REG_R5)!=12)return;
      uint32_t root=r->r32(r->reg(UC_ARM_REG_R4)),record=r->r32(root+0x4e8)+12*28;
      if(r->r16(root+0x4e4)!=kOfflineItemCount||r->r16(record)!=4024)
        throw std::runtime_error("Unexpected original artifact eligibility record");
      uint8_t flag,count;r->read(record+7,&flag,1);r->read(root+0x502+flag,&count,1);
      r->setreg(regs[0],count);r->setreg(UC_ARM_REG_PC,BASE+0x13730);return;
    }
    if(offset==0x1ab24){
      uint32_t modal=r->reg(UC_ARM_REG_R4),root=r->r32(modal),item=r->r32(modal+4);
      if(item>=kOfflineItemCount||item>=r->r16(root+0x4e4)||r->r16(r->r32(root+0x4e8)+item*28)!=4000+item*2)
        throw std::runtime_error("Unexpected offline item in confirmation UI item="+std::to_string(item));
      uint8_t count;r->read(r->reg(UC_ARM_REG_R8)+0x293c+item,&count,1);
      if(item==12){uint8_t flag;r->read(r->r32(root+0x4e8)+item*28+7,&flag,1);r->read(root+0x502+flag,&count,1);}
      r->setreg(regs[1],count);return;
    }
    uint32_t modal=r->reg(UC_ARM_REG_R4),root=r->r32(modal),item=r->r32(modal+4);
    if(item>=kOfflineItemCount||item>=r->r16(root+0x4e4)||r->r16(r->r32(root+0x4e8)+item*28)!=4000+item*2)
      throw std::runtime_error("Unexpected original offline item record");
    if(offset==0x10c10){
      uint8_t state;r->read(modal+8,&state,1);
      if(r->reg(regs[0])!=modal||state!=0)throw std::runtime_error("Unexpected original item confirmation state");
      // This site is reached only AFTER the user confirms the free prompt and
      // the original duplicate/eligibility check passes. Jump to the original
      // reward path in THIS handler, retaining its stack, grant counters,
      // auto-save and modal dismissal. No payment reply or SMS is simulated.
      __android_log_print(ANDROID_LOG_INFO,"XuanyuanOffline","local free confirmation item=%u",item);
      r->setreg(UC_ARM_REG_PC,BASE+0x10c4c);return;
    }
    if(offset==0x10c98){
      // Retired SMS bookkeeping is not part of the offline grant. Its last
      // slot overlaps the flight permission; resetting it would revoke flight.
      // Keep the original reward, acquisition counter and Save(8) below.
      r->setreg(UC_ARM_REG_PC,BASE+0x10cac);return;
    }
    if(offset==0x10cb8){
      uint8_t count;r->read(r->app+0x293c+item,&count,1);
      r->pendingOfflineSave=root;r->soundEvents|=1u<<10;
      __android_log_print(ANDROID_LOG_INFO,"XuanyuanOffline","original reward granted item=%u count=%u",item,count);
    }
  }catch(const std::exception&e){r->fail(e.what());}}
  static void uiBackgroundThunk(uc_engine*,uint64_t,uint32_t,void* user){auto*r=(Runtime*)user;try{
    uint32_t gfx=r->reg(regs[0]);r->battleBackdropClear=false;
    if(r->r32(r->r32(gfx+0x10))==r->framebuffer)r->fillTint=rgb565(r->r32(r->reg(UC_ARM_REG_SP)+8)&65535);
    if(r->inBattle()&&r->reg(UC_ARM_REG_LR)==BASE+0x18b94&&r->reg(regs[1])==0&&r->reg(regs[2])==0&&r->reg(regs[3])==240){
      auto it=r->textures.find(uint64_t(3)<<32);if(it!=r->textures.end()){r->canvas=it->second.pixels;r->battleBackdropClear=true;}
    }
    // These six original UI fills retain a 128px width while text uses the
    // actual display width. Expand their matching outer/inner rectangles only.
    uint32_t caller=r->reg(UC_ARM_REG_LR)-BASE,expected=0;
    if(caller==0x1b008||caller==0xa938)expected=128;
    else if(caller==0x1b060||caller==0xa97c)expected=124;
    else if(caller==0xa9e0)expected=118;
    else if(caller==0xaa14)expected=114;
    if(!expected||r->reg(regs[3])!=expected)return;
    if(r->r32(r->r32(gfx+0x10))!=r->framebuffer)return;
    uint32_t width=r->r16(gfx);
    if(width>128&&width<=240)r->setreg(regs[3],expected+width-128);
  }catch(const std::exception&e){r->fail(e.what());}}
  bool inBattle(){if(!app||r32(app+0x2980)!=5)return false;uint32_t v=r32(app+0x2984);return v&&((r16(v+0x1d8)&255)==11)&&r32(v+0x70);}
  uint32_t battle(){return inBattle()?r32(r32(app+0x2984)+0x70):0;}
  void audioState(){
    uint32_t scene=1;
    if(app&&r32(app+0x2980)==5){uint32_t v=r32(app+0x2984),root=v?r32(v+0x68):0;
      if(root){uint32_t stage=r16(root+0x4f8);scene=stage<=10?2:stage<100?3:stage<300?4:5;
        if(lastStage&&stage!=lastStage)soundEvents|=1u<<8;
        lastStage=stage;
        if(!inBattle()){uint32_t hero=r32(root+0x167c);if(hero){int x=int16_t(r16(hero+0x18e)),y=int16_t(r16(hero+0x190));if((x!=oldHeroX||y!=oldHeroY)&&clockMs-lastStep>190){if(std::abs(x-oldHeroX)+std::abs(y-oldHeroY)<80)soundEvents|=1u<<3;lastStep=clockMs;}oldHeroX=x;oldHeroY=y;}}
      }
      uint32_t b=battle();if(b){scene=6;
        for(int i=0;i<6;i++){uint32_t a=r32(b+(i<3?i*4:0x10+(i-3)*4));if(!a)continue;int32_t hp=int32_t(r32(a+0x31c));auto old=unitHealth.find(a);if(old!=unitHealth.end()&&old->second!=hp){soundEvents|=1u<<(hp<old->second?5:7);if(hp<old->second)soundEvents|=1u<<4;}unitHealth[a]=hp;if(i>=3&&hp>20000)bossMusic=true;}
        scene=bossMusic?7:6;uint8_t state=0,won=0;read(b+0x4c,&state,1);read(b+0xb1,&won,1);if(state==4&&won==1&&!victoryPlayed){soundEvents|=1u<<11;victoryPlayed=true;}
      }
    }
    lastSoundScene=scene;
  }
  void raster(const Draw&d,const Transform&a,int cl=0,int ct=0,int cr=240,int cb=320){
    auto it=textures.find(d.key);if(it==textures.end())return;auto&t=it->second;float sc=a.active?a.scale:1;
    float x=a.active?a.nx+(d.x-a.ox)*sc:d.x,y=a.active?a.ny+(d.y-a.oy)*sc:d.y;
    // The original 240x320 clip must not discard sprites moved away from edges.
    int l=std::max(0,int(std::floor(x*SCALE))),top=std::max(0,int(std::floor(y*SCALE)));
    int right=std::min(WIDTH,int(std::ceil((x+d.w*sc)*SCALE))),bottom=std::min(HEIGHT,int(std::ceil((y+d.h*sc)*SCALE)));
    if(!a.active){l=std::max(l,cl*SCALE);top=std::max(top,ct*SCALE);right=std::min(right,cr*SCALE);bottom=std::min(bottom,cb*SCALE);}
    for(int yy=top;yy<bottom;yy++)for(int xx=l;xx<right;xx++){
      int tx=int((xx+.5f-x*SCALE)/sc),ty=int((yy+.5f-y*SCALE)/sc);if(tx<0||ty<0||tx>=d.w*SCALE||ty>=d.h*SCALE)continue;
      if(d.flags&2)tx=d.w*SCALE-1-tx;if(d.flags&4)ty=d.h*SCALE-1-ty;tx+=d.sx*SCALE;ty+=d.sy*SCALE;
      if(tx<0||ty<0||tx>=t.width||ty>=t.height)continue;uint32_t src=t.pixels[ty*t.width+tx];int alpha=(src>>24)*d.opacity/255;
      if(d.flags&16)src=(src&0xff000000)|(blend(src,d.tint,d.mix)&0xffffff);auto&dst=canvas[yy*WIDTH+xx];dst=blend(dst,src,alpha);
    }
  }
  void effects(){
    collectingEffects=false;if(effectDraws.empty())return;
    float l=10000,t=10000,right=-10000,bottom=-10000;
    for(const auto&d:effectDraws){l=std::min(l,float(d.x));t=std::min(t,float(d.y));right=std::max(right,float(d.x+d.w));bottom=std::max(bottom,float(d.y+d.h));}
    float sc=std::min({1.55f,208/std::max(1.f,right-l),226/std::max(1.f,bottom-t)});
    Transform a{(l+right)/2,(t+bottom)/2,120,145,sc,true};for(const auto&d:effectDraws)raster(d,a);
    battleEffectGroups++;
    std::ifstream flag(saveDir+"/capture-effects.flag");if(flag){flag.close();std::ofstream f(saveDir+"/battle-effect.argb",std::ios::binary);f.write((const char*)canvas.data(),canvas.size()*4);std::ofstream meta(saveDir+"/battle-effect.txt");meta<<"source_bounds="<<l<<","<<t<<","<<right<<","<<bottom<<" scale="<<sc<<" center=120,145 count="<<effectDraws.size()<<"\n";std::remove((saveDir+"/capture-effects.flag").c_str());}
    effectDraws.clear();
  }
  static void presentationThunk(uc_engine*,uint64_t address,uint32_t,void*user){auto*r=(Runtime*)user;try{
    uint32_t off=address-BASE;
    if(off==0x13164){r->unitOrigins.clear();r->unitHealth.clear();r->effectDraws.clear();r->previousEffects.clear();r->currentEffects.clear();r->victoryPlayed=false;r->bossMusic=false;r->selectedBattleCommand=0;return;}
    if(off==0x93d4){r->transform.active=false;return;}
    if(off==0x1941c){r->collectingEffects=false;return;}
    if(off==0x18c84){r->effects();return;}
    uint32_t b=r->battle();if(!b)return;
    if(off==0x19134){if(r->reg(UC_ARM_REG_LR)==BASE+0x18c78){r->collectingEffects=true;uint32_t actor=r->reg(regs[0]);r->currentEffects.insert(actor);if(!r->previousEffects.count(actor))r->soundEvents|=1u<<6;}return;}
    if(off!=0x9068)return;uint32_t actor=r->reg(regs[0]);int side=-1,slot=-1,count=0;
    for(int group=0;group<2;group++)for(int i=0;i<3;i++)if(r->r32(b+(group?0x10:0)+i*4)==actor){side=group;slot=i;}
    if(side<0)return;for(int i=0;i<3;i++)if(r->r32(b+(side?0x10:0)+i*4))count++;
    int rank=0;for(int i=0;i<slot;i++)if(r->r32(b+(side?0x10:0)+i*4))rank++;
    float ox=int16_t(r->r16(actor+0x18e)),oy=int16_t(r->r16(actor+0x190));auto original=r->unitOrigins.emplace(actor,std::make_pair(ox,oy)).first->second;
    float ny=count==1?180:count==2?138+rank*82:98+rank*82;
    r->transform={original.first,original.second,side?62.f:178.f,ny,1.35f,true};r->battleUnitDraws++;
  }catch(const std::exception&e){r->fail(e.what());}}
  static uint32_t blend(uint32_t dst,uint32_t src,int alpha){if(alpha<=0)return dst;if(alpha>=255)return 0xff000000|(src&0xffffff);uint32_t out=0xff000000;for(int shift:{0,8,16})out|=((((src>>shift)&255)*alpha+((dst>>shift)&255)*(255-alpha)+127)/255)<<shift;return out;}
  void blit(){
    hdSprite=false;battleBackdropClear=false;uint32_t gfx=reg(regs[0]);if(r32(r32(gfx+0x10))!=framebuffer)return;
    uint32_t args[7];read(reg(UC_ARM_REG_SP),args,sizeof(args));auto bind=bindings.find(args[1]);uint32_t descriptor[8];read(args[1],descriptor,sizeof(descriptor));char trace[420];std::snprintf(trace,sizeof(trace),"last blit gfx=0x%x xy=%d,%d wh=%d,%d texture=0x%x source=%d,%d flags=0x%x id=0x%llx descriptor=%x,%x,%x,%x,%x,%x,%x,%x caller=0x%x",gfx,int32_t(reg(regs[1])),int32_t(reg(regs[2])),int32_t(reg(regs[3])),int32_t(args[0]),args[1],int32_t(args[2]),int32_t(args[3]),args[4],(unsigned long long)(bind==bindings.end()?0:bind->second),descriptor[0],descriptor[1],descriptor[2],descriptor[3],descriptor[4],descriptor[5],descriptor[6],descriptor[7],reg(UC_ARM_REG_LR)-BASE);lastBlit=trace;
    if(bind==bindings.end())return;auto image=textures.find(bind->second);if(image==textures.end())return;
    int16_t fields[8];read(gfx,fields,sizeof(fields));int x=int32_t(reg(regs[1]))+fields[6],y=int32_t(reg(regs[2]))+fields[7];int w=int32_t(reg(regs[3])),h=int32_t(args[0]),sx=int32_t(args[2]),sy=int32_t(args[3]);uint32_t flags=args[4];
    if(w<=0||h<=0)return;hdSprite=true;if(!rendering)return;
    int opacity=(flags&8)?std::min(32u,args[5])*255/32:255;
    uint32_t tint=rgb565(args[6]&65535);int mix=std::min((args[6]>>16)&255,32u)*255/32;
    Draw draw{bind->second,x,y,w,h,sx,sy,flags,tint,opacity,mix};
    if(collectingEffects)effectDraws.push_back(draw);
    else raster(draw,transform,int(fields[2]),int(fields[3]),int(fields[4]),int(fields[5]));
    hdBlits++;
  }
  static void glyphThunk(uc_engine*,uint64_t,uint32_t,void* user){auto*r=(Runtime*)user;try{r->glyph();}catch(const std::exception&e){r->fail(e.what());}}
  void glyph(){
    if(!rendering)return;uint32_t sp=reg(UC_ARM_REG_SP),gfx=r32(sp+0xc8);if(r32(r32(gfx+0x10))!=framebuffer)return;
    uint16_t code=r16(r32(sp+0xb8));auto found=glyphOffsets.find(code);if(found==glyphOffsets.end())found=glyphOffsets.find(0x25a1);if(found==glyphOffsets.end())return;
    int x=int32_t(reg(UC_ARM_REG_R7)),y=int32_t(r32(sp+0xfc));int left=int32_t(reg(UC_ARM_REG_R5)),top=int32_t(reg(UC_ARM_REG_R6)),right=int32_t(r32(sp+0x8c)),bottom=int32_t(r32(sp+0x88));
    if(!transform.active&&inBattle()&&x>=90&&x<=145&&y>=120&&y<=190){
      auto art=textures.find((uint64_t(3)<<32)|code);if(art!=textures.end()){
        float cy=166+(y+6-166)*1.55f;bool selected=y>=154&&y<=162;if(selected)selectedBattleCommand=code;float size=selected?30.f:26.f;Draw d{(uint64_t(3)<<32)|code,0,0,32,32,0,0,0,0,selected?255:175,0};Transform a{0,0,120-size/2,cy-size/2,size/32,true};raster(d,a,0,0,240,320);battleCommandDraws++;hdGlyphs++;return;
      }
    }
    int w=code<256?36:72;size_t offset=found->second;if(offset+5184>glyphs.size())return;
    const uint8_t*mask=glyphs.data()+offset;bool outline=(r32(sp+0x30)&15)!=0;uint32_t stroke=rgb565(r32(sp+0x2c));
    for(int yy=0;yy<72;yy++)for(int xx=0;xx<w;xx++){
      int dx=x*SCALE+xx,dy=y*SCALE+yy;if(!transform.active&&(dx<std::max(0,left*SCALE)||dx>=std::min(WIDTH,right*SCALE)||dy<std::max(0,top*SCALE)||dy>=std::min(HEIGHT,bottom*SCALE)))continue;
      uint8_t alpha=mask[yy*72+xx];uint32_t color=rgb565(r32(sp+0x34+(yy/SCALE)*4));
      if(transform.active){
        float sc=transform.scale;int l=int((transform.nx+(float(dx)/SCALE-transform.ox))*SCALE),t=int((transform.ny+(float(dy)/SCALE-transform.oy))*SCALE);
        for(int py=std::max(0,t);py<std::min(HEIGHT,int(t+std::ceil(sc)));py++)for(int px=std::max(0,l);px<std::min(WIDTH,int(l+std::ceil(sc)));px++)canvas[py*WIDTH+px]=blend(canvas[py*WIDTH+px],color,alpha);
        continue;
      }
      auto&dst=canvas[dy*WIDTH+dx];
      if(outline){int a=0;for(int oy=-2;oy<=2;oy++)for(int ox=-2;ox<=2;ox++)if(xx+ox>=0&&xx+ox<w&&yy+oy>=0&&yy+oy<72)a=std::max(a,int(mask[(yy+oy)*72+xx+ox]));dst=blend(dst,stroke,a);}
      dst=blend(dst,color,alpha);
    }
    hdGlyphs++;
  }
};
std::unique_ptr<Runtime> runtime;
std::string initError;
}

extern "C" JNIEXPORT jstring JNICALL Java_org_xuanyuan_originalhd_MainActivity_nativeCreate(JNIEnv*env,jclass,jobject assets,jstring dir){try{
  const char*s=env->GetStringUTFChars(dir,nullptr);std::string directory(s);env->ReleaseStringUTFChars(dir,s);runtime=std::make_unique<Runtime>(AAssetManager_fromJava(env,assets),directory);initError.clear();return env->NewStringUTF("");
}catch(const std::exception&e){initError=e.what();return env->NewStringUTF(initError.c_str());}}
extern "C" JNIEXPORT void JNICALL Java_org_xuanyuan_originalhd_MainActivity_nativeTexture(JNIEnv*env,jclass,jint pack,jint id,jint w,jint h,jintArray data){if(!runtime)return;Texture t{w,h,{}};t.pixels.resize(size_t(w)*h);env->GetIntArrayRegion(data,0,t.pixels.size(),(jint*)t.pixels.data());runtime->textures[(uint64_t(pack)<<32)|uint32_t(id)]=std::move(t);}
extern "C" JNIEXPORT void JNICALL Java_org_xuanyuan_originalhd_MainActivity_nativeFont(JNIEnv*env,jclass,jbyteArray data,jintArray codes){if(!runtime)return;runtime->glyphs.resize(env->GetArrayLength(data));env->GetByteArrayRegion(data,0,runtime->glyphs.size(),(jbyte*)runtime->glyphs.data());std::vector<jint> chars(env->GetArrayLength(codes));env->GetIntArrayRegion(codes,0,chars.size(),chars.data());for(size_t i=0;i<chars.size();i++)runtime->glyphOffsets[chars[i]]=i*5184;}
extern "C" JNIEXPORT jstring JNICALL Java_org_xuanyuan_originalhd_MainActivity_nativeBoot(JNIEnv*env,jclass){try{if(runtime)runtime->boot();}catch(const std::exception&e){if(runtime)runtime->fail(e.what());}return env->NewStringUTF(runtime?runtime->error.c_str():initError.c_str());}
extern "C" JNIEXPORT jstring JNICALL Java_org_xuanyuan_originalhd_MainActivity_nativeTick(JNIEnv*env,jclass,jint elapsed,jintArray pixels){try{if(runtime){runtime->tick(elapsed);env->SetIntArrayRegion(pixels,0,runtime->canvas.size(),(jint*)runtime->canvas.data());}}catch(const std::exception&e){runtime->fail(e.what());}return env->NewStringUTF(runtime?runtime->error.c_str():initError.c_str());}
extern "C" JNIEXPORT void JNICALL Java_org_xuanyuan_originalhd_MainActivity_nativeKey(JNIEnv*,jclass,jint code,jboolean pressed){try{if(runtime)runtime->key(code,pressed);}catch(const std::exception&e){runtime->fail(e.what());}}
extern "C" JNIEXPORT jstring JNICALL Java_org_xuanyuan_originalhd_MainActivity_nativeStats(JNIEnv*env,jclass){char s[320];if(runtime){runtime->captureRequested();std::snprintf(s,sizeof(s),"frames=%u hdBlits=%llu hdGlyphs=%llu decodedImages=%zu pendingTimers=%zu battleUnits=%llu effectGroups=%llu commandArt=%llu scene=%u error=%s",runtime->frames,(unsigned long long)runtime->hdBlits,(unsigned long long)runtime->hdGlyphs,runtime->verifiedImages.size(),runtime->timers.size(),(unsigned long long)runtime->battleUnitDraws,(unsigned long long)runtime->battleEffectGroups,(unsigned long long)runtime->battleCommandDraws,runtime->lastSoundScene,runtime->error.c_str());}else std::snprintf(s,sizeof(s),"%s",initError.c_str());return env->NewStringUTF(s);}

extern "C" JNIEXPORT jint JNICALL Java_org_xuanyuan_originalhd_MainActivity_nativeAudio(JNIEnv*,jclass){if(!runtime)return 0;runtime->audioState();uint32_t events=runtime->soundEvents;runtime->soundEvents=0;return (runtime->lastSoundScene<<16)|events|(runtime->quitRequested?0x01000000u:0);}
