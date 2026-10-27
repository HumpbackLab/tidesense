#include "../preview_encoder.hpp"
struct PreviewEncoder::Impl {unsigned width,height;};
PreviewEncoder::PreviewEncoder(unsigned w,unsigned h):impl_(new Impl{2*w,h}){}
PreviewEncoder::~PreviewEncoder()=default;
unsigned PreviewEncoder::width()const{return impl_->width;}
unsigned PreviewEncoder::height()const{return impl_->height;}
std::vector<uint8_t> PreviewEncoder::encode(const LivePair& pair) {
 // Deliberately incompressible payload fills socket buffers for slow readers.
 std::vector<uint8_t> bytes(256*1024,uint8_t(pair.header.sequence));
 bytes[0]=0xff;bytes[1]=0xd8;bytes[bytes.size()-2]=0xff;bytes.back()=0xd9;
 return bytes;
}
