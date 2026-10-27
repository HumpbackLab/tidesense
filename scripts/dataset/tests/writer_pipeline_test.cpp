#include "../writer.hpp"
#include <dlfcn.h>
#include <future>

std::atomic<bool> damage_encoder{false};
extern "C" size_t __real_ZSTD_compress2(ZSTD_CCtx*,void*,size_t,const void*,size_t);
extern "C" size_t __wrap_ZSTD_compress2(ZSTD_CCtx* ctx,void* out,size_t capacity,const void* input,size_t size) {
 size_t bytes=__real_ZSTD_compress2(ctx,out,capacity,input,size);
 if (damage_encoder && !ZSTD_isError(bytes) && bytes) static_cast<uint8_t*>(out)[bytes-1]^=1;
 return bytes;
}

namespace {
struct FaultRelease { void(*mode)(int); ~FaultRelease() {mode(0);} };
void require(bool condition,const char* reason) {if(!condition) throw std::runtime_error(reason);}
template<typename Test> void wait(Test condition) {
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(!condition()) {
  require(std::chrono::steady_clock::now()<deadline,"Timed out waiting for writer stage");
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
}
void submit(Writer& writer,unsigned sequence) {
 LivePair pair;
 uint64_t stamp=1000000000ULL+(sequence-1)*50000000ULL;
 pair.header={CAMERA_MAGIC,CAMERA_VERSION,640,400,sequence,stamp,stamp+2000000,sequence,sequence,CAMERA_FSIN,0};
 cv::Mat* images[]={&pair.left,&pair.right};
 for(unsigned camera=0;camera<2;++camera) {
  images[camera]->create(400,640,CV_8UC1);
  for(unsigned y=0;y<400;++y) for(unsigned x=0;x<640;++x)
   images[camera]->ptr(y)[x]=uint8_t(x*31+y*17+sequence*13+camera*97);
 }
 std::vector<ImuSample> imu;
 for(unsigned i=0;i<10;++i) {
  ImuSample sample;sample.stamp_ns=stamp-5000000+i*5000000;
  sample.accel={{1.,2.,9.8}};imu.push_back(sample);
 }
 writer.submit(std::move(pair),std::move(imu));
}
void verify(const std::string& path,unsigned count) {
 std::ifstream file(path+"/frames.bin",std::ios::binary);
 char magic[17];file.read(magic,sizeof(magic));
 require(!std::memcmp(magic,"TIDESENSE_ZST_V1\n",17),"Archive magic changed");
 for(unsigned sequence=1;sequence<=count;++sequence) {
  uint32_t bytes;file.read(reinterpret_cast<char*>(&bytes),sizeof(bytes));
  require(file.good() && bytes<=ZSTD_compressBound(512056),"Invalid encoded record length");
  std::vector<uint8_t> encoded(bytes),raw(512056);file.read(reinterpret_cast<char*>(encoded.data()),bytes);
  size_t size=ZSTD_decompress(raw.data(),raw.size(),encoded.data(),encoded.size());
  require(!ZSTD_isError(size) && size==raw.size(),"Frame decompression/checksum failed");
  CameraHeader h{};std::memcpy(&h,raw.data(),sizeof(h));
  require(h.sequence==sequence && h.left_ns==1000000000ULL+(sequence-1)*50000000ULL,"Frame order/time changed");
  size_t position=sizeof(h);
  for(unsigned camera=0;camera<2;++camera) for(unsigned y=0;y<400;++y) {
   uint8_t pixel=0;
   for(unsigned x=0;x<640;++x) {
    pixel=uint8_t(pixel+raw[position++]);
    require(pixel==uint8_t(x*31+y*17+sequence*13+camera*97),"Pixel roundtrip changed");
   }
  }
 }
 require(file.peek()==std::ifstream::traits_type::eof(),"Unexpected extra record");
 std::ifstream all(path+"/frames.bin",std::ios::binary);
 std::vector<char> bytes((std::istreambuf_iterator<char>(all)),{});
 require(YAML::LoadFile(path+"/metadata.yaml")["expected_frames_xxh64"].as<uint64_t>()==XXH64(bytes.data(),bytes.size(),0),"Recorded source digest differs from file");
}
}
int main(int argc,char** argv) {
 (void)interrupted; (void)&interrupt;
 if(argc!=3) return 2;
 auto mode=reinterpret_cast<void(*)(int)>(dlsym(RTLD_DEFAULT,"writer_fault_mode"));
 auto blocked=reinterpret_cast<bool(*)()>(dlsym(RTLD_DEFAULT,"writer_fault_blocked"));
 if(!mode || !blocked) return 3;
 try {
  std::string base=std::string(argv[2])+"/writer-test-XXXXXX";
  require(mkdtemp(&base[0]),"Cannot create temporary test directory");
  std::string path=base+"/capture",test=argv[1];
  tide_cam_config config{};config.mode={640,400,TIDE_CAM_MONO8};
  if(test=="blocked") {
   Writer writer(path,32,config);FaultRelease release{mode};mode(1);submit(writer,1);wait([&]{return blocked();});
   for(unsigned n=2;n<=20;++n) submit(writer,n);
   // All remaining frames must compress even while the file write is held.
   wait([&]{return writer.statistics().encoded==19;});
   require(writer.statistics().pending==20,"Compressed packets bypassed the shared capacity bound");
   auto finish=std::async(std::launch::async,[&]{writer.finish(true,"");});
   bool waiting=finish.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout;
   bool incomplete=access((path+"/COMPLETE").c_str(),F_OK)!=0;
   mode(0);finish.get();
   require(waiting,"Finish did not wait for the disk");
   require(incomplete,"COMPLETE appeared before disk release");
   require(YAML::LoadFile(path+"/metadata.yaml")["pairs"].as<unsigned>()==20,"Frame order/count lost during drain");
   verify(path,20);
  } else if(test=="overflow") {
   Writer writer(path,4,config);FaultRelease release{mode};mode(1);submit(writer,1);wait([&]{return blocked();});
   for(unsigned n=2;n<=4;++n) submit(writer,n);
   wait([&]{return writer.statistics().encoded==3;});
   std::string error;
   try {submit(writer,5);} catch(const std::runtime_error& e) {error=e.what();}
   require(error.find("Recording queue full")!=std::string::npos,"Encoded queue did not obey capacity");
   mode(0);
   try {writer.finish(false,error);} catch(const std::runtime_error&) {}
   require(access((path+"/COMPLETE").c_str(),F_OK)!=0,"Overflow was presented as complete");
   require(YAML::LoadFile(path+"/metadata.yaml")["pairs"].as<unsigned>()==4,"Accepted prefix was not drained");
   verify(path,4);
  } else if(test=="error") {
   Writer writer(path,8,config);FaultRelease release{mode};mode(2);submit(writer,1);
   bool failed=false;
   try {writer.finish(true,"");} catch(const std::exception&) {failed=true;}
   mode(0);
   require(failed,"Disk error did not propagate");
   require(access((path+"/INCOMPLETE").c_str(),F_OK)==0,"Disk error lost incomplete marker");
   require(access((path+"/COMPLETE").c_str(),F_OK)!=0,"Disk error was presented as complete");
   require(!YAML::LoadFile(path+"/metadata.yaml")["writer_error"].as<std::string>().empty(),"Final metadata lost disk failure");
  } else if(test=="blocked_sync") {
   Writer writer(path,8,config);FaultRelease release{mode};mode(4);submit(writer,1);
   auto finish=std::async(std::launch::async,[&]{writer.finish(true,"");});
   wait([&]{return blocked();});
   bool waiting=finish.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout;
   bool incomplete=access((path+"/COMPLETE").c_str(),F_OK)!=0;
   mode(0);finish.get();
   require(waiting && incomplete,"COMPLETE was published before fsync finished");
   auto meta=YAML::LoadFile(path+"/metadata.yaml");
   require(meta["complete"].as<bool>() && meta["encoding_verified"].as<bool>() && !meta["archive_verified"].as<bool>(),"Durability and archive validation were conflated");
   verify(path,1);
  } else if(test=="stored_corruption") {
   Writer writer(path,8,config);FaultRelease release{mode};mode(3);submit(writer,1);
   bool failed=false;
   try{writer.finish(true,"");}catch(const std::exception& e){failed=std::string(e.what()).find("Written frame")!=std::string::npos;}
   mode(0);
   require(failed,"Successful write with corrupt file bytes escaped immediate readback");
   require(access((path+"/COMPLETE").c_str(),F_OK)!=0,"Corrupt written bytes published COMPLETE");
  } else if(test=="codec_corruption" || test=="sync_error") {
   Writer writer(path,8,config);FaultRelease release{mode};
   mode(test=="sync_error"?5:0);damage_encoder=test=="codec_corruption";
   submit(writer,1);
   bool failed=false;
   try {writer.finish(true,"");} catch(const std::exception&) {failed=true;}
   mode(0);damage_encoder=false;
   require(failed,"Encoding/sync failure was accepted");
   require(access((path+"/COMPLETE").c_str(),F_OK)!=0,"Failed capture published COMPLETE");
   auto meta=YAML::LoadFile(path+"/metadata.yaml");
   require(!meta["complete"].as<bool>() && !meta["archive_verified"].as<bool>(),"Failed capture marked verified");
   std::string reason=meta["writer_error"].as<std::string>();
   require(reason.find(test=="sync_error"?"Cannot sync":"before queue")!=std::string::npos,"Failure stage was not identified");
  } else return 4;
  std::cout<<"capture="<<path<<'\n';
  return 0;
 } catch(const std::exception& e) {
  mode(0);std::cerr<<e.what()<<'\n';return 1;
 }
}
