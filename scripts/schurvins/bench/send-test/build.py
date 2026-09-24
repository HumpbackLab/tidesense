#!/usr/bin/env python3
"""Build isolated entry point using existing all-six-optimizations static libraries."""
import pathlib, shlex, subprocess, json, hashlib
here=pathlib.Path(__file__).resolve().parent
root=here.parents[3]
tree=root/'install/schurvins-gcc11-static/opt-all'
out=here/'build'
out.mkdir(exist_ok=True)
source=(root/'scripts/schurvins/stereo.cpp').read_text()
source=source.replace('int main(int argc, char** argv) {','#include "sender.h"\n#include <deque>\n\nint main(int argc, char** argv) {')
source=source.replace('    while (true) {','''    BenchSender sender;
    std::ofstream send_timing;
    if (const char* p=std::getenv("SCHUR_SEND_TIMING")) send_timing.open(p);
    if(send_timing) send_timing << "frame,submit_ms,step_ms,thread_cpu_ms\\n";
    std::deque<std::pair<std::vector<std::string>,std::vector<cv::Mat>>> cache;
    while (true) {
      auto step_start=clockNs(CLOCK_MONOTONIC);
      auto thread_start=clockNs(CLOCK_THREAD_CPUTIME_ID);''')
source=source.replace('        auto r = row(images);', '''        if(cache.empty()) {
          for(size_t n=0;n<64;++n) {
            auto r=row(images); if(r.empty()) break;
            if(r.size()!=3) throw std::runtime_error("Bad image CSV");
            std::vector<cv::Mat> loaded={cv::imread(path(argv[2],r[1]),cv::IMREAD_GRAYSCALE),
                                        cv::imread(path(argv[2],r[2]),cv::IMREAD_GRAYSCALE)};
            cache.emplace_back(std::move(r),std::move(loaded));
          }
        }
        if(cache.empty()) break;
        auto cached=std::move(cache.front()); cache.pop_front();
        auto r=std::move(cached.first);
        step_start=clockNs(CLOCK_MONOTONIC);
        thread_start=clockNs(CLOCK_THREAD_CPUTIME_ID);''')
source=source.replace('''        pair = {cv::imread(path(argv[2],r[1]),cv::IMREAD_GRAYSCALE),
                cv::imread(path(argv[2],r[2]),cv::IMREAD_GRAYSCALE)};''',
                      '        pair = std::move(cached.second);')
source=source.replace('      ++tracked;','''      ++tracked;
      const double packet_pose[7]={p.x(),p.y(),p.z(),q.x(),q.y(),q.z(),q.w()};
      auto submit_start=clockNs(CLOCK_MONOTONIC);
      sender.submit(t,packet_pose,pair[0]);
      auto submit_end=clockNs(CLOCK_MONOTONIC);
      if(send_timing) send_timing << frames-1 << ',' << (submit_end-submit_start)/1e6
        << ',' << (submit_end-step_start)/1e6 << ','
        << (clockNs(CLOCK_THREAD_CPUTIME_ID)-thread_start)/1e6 << '\\n';''')
(out/'stereo_send.cpp').write_text(source)
flags={}
for line in (tree/'CMakeFiles/schurvins_stereo.dir/flags.make').read_text().splitlines():
    if ' = ' in line:
        key,value=line.split(' = ',1); flags[key]=shlex.split(value)
link=shlex.split((tree/'CMakeFiles/schurvins_stereo.dir/link.txt').read_text())
obj=out/'stereo_send.o'
subprocess.run([link[0],*flags['CXX_DEFINES'],*flags['CXX_INCLUDES'],*flags['CXX_FLAGS'],
                '-I'+str(here),'-c',str(out/'stereo_send.cpp'),'-o',str(obj)],check=True)
link=[str(obj) if x=='CMakeFiles/schurvins_stereo.dir/stereo.cpp.o' else x for x in link]
link[link.index('-o')+1]=str(out/'schurvins_send_test')
subprocess.run(link,cwd=tree,check=True)
subprocess.run([link[0].removesuffix('g++')+'strip',str(out/'schurvins_send_test')],check=True)
files=[tree/'libschurvins.a',tree/'libfast.a',out/'schurvins_send_test',here/'sender.h',root/'scripts/schurvins/stereo.cpp']
(out/'artifacts.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},indent=2))
