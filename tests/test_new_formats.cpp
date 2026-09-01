#include "core/rawrecord.h"
#include "core/dataset.h"
#include "core/aimodel.h"
#include <cassert>
#include <vector>
#include <filesystem>
int main(){
  RawSessionHeader h; h.sampleRate=500; h.channels={{0,"ch0"},{2,"ch2"}};
  RawSessionWriter w; assert(w.open("/tmp/test_raw_v2.bsig",h)); w.writeBlock({{1,2},{3,4}}); w.close();
  RawSessionReader r; assert(r.open("/tmp/test_raw_v2.bsig")); std::vector<int32_t>s; assert(r.readSampleSet(1,s)&&s[0]==2&&s[1]==4);
  DatasetSpec spec; spec.taskId="t1"; spec.displayName="A \"task\""; spec.channels={0,2}; spec.windowSamples=2; spec.strideSamples=2; DatasetWriter d; assert(d.open("/tmp/test_data.bset",spec)); assert(d.append("gesture\"x",{{1,2},{3,4}},0)); d.finish(); DatasetSpec loaded; assert(loadDatasetManifest("/tmp/test_data.bset",loaded)); assert(loaded.taskId=="t1"&&loaded.windowSamples==2); auto bundle=nextModelBundlePath("/tmp/test_models"); assert(writeModelManifest(bundle,spec,"/tmp/test_data.bset","weights.json")); std::ofstream("/tmp/test_models/dummy");
}
