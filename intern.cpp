#include <atomic>
#include <memory>
#include <string>
#include <stdio.h>

#ifdef _MSC_VER
#include <concurrent_unordered_set.h>
using concurrency::concurrent_unordered_set;
#else
#include <tbb/concurrent_unordered_set.h>
using tbb::concurrent_unordered_set;
#endif

class StringPool {
  std::atomic<uint32_t> m_poolUsed;
  uint32_t m_poolSize;
  std::unique_ptr<char[]> m_poolData;

  concurrent_unordered_set<std::string_view, std::hash<std::string_view>>
      m_storage;

  std::atomic<uint64_t> m_usedCountAndSizeStats;
  std::atomic<uint64_t> m_leakCountAndSizeStats;

  std::atomic<uint64_t> m_refCountStats;
  std::atomic<uint64_t> m_refSizeStats;

  // Places a string in the next availabe position into the data pool.
  // If there is not enough space, returns nullptr,
  // otherwise returns pointer to the location where the string was copied.
  //
  // The operation is threadsafe, using an atomic index for pool allocation.
  const char *PoolString(std::string_view s) {
    const auto stringLength = s.length();
    const auto dataLength = stringLength + 1;
    const auto dataOffset = m_poolUsed.fetch_add(dataLength);
    if (dataOffset + dataLength >= m_poolSize)
      return nullptr;

    auto data = m_poolData.get() + dataOffset;
    memcpy(data, s.data(), stringLength);
    data[stringLength] = 0;
    return data;
  }

public:
  explicit StringPool(size_t size, std::unique_ptr<char[]> &&pool)
      : m_poolSize(size), m_poolData(std::move(pool)) {
    m_poolUsed.store(0);
    m_usedCountAndSizeStats.store(0);
    m_leakCountAndSizeStats.store(0);
    m_refCountStats.store(0);
    m_refSizeStats.store(0);
  }

  std::string_view InternString(std::string_view s) {
    // Lookup the input string_view, and if found
    // return the pooled one.
    const auto it = m_storage.find(s);
    if (it != m_storage.cend()) {
      // Increase the number of times any string has been referenced more than
      // once.
      m_refCountStats.fetch_add(1);
      m_refSizeStats.fetch_add((*it).length() + 1);
      return *it;
    }

    // If not, try to pool the string, if there is space.
    // If there is no space, return nullptr.
    // NOTE: Trying to pool the same string twice from
    // different threads would result in racing condition
    // but one of them is getting in. At worst, the loss
    // is the N duplicates, where N is the number of cpus.
    const auto pooledString = PoolString(s);
    if (!pooledString)
      return nullptr;

    const auto insertPair = m_storage.insert({pooledString, s.length()});
    if (insertPair.second) {
      m_usedCountAndSizeStats.fetch_and((uint64_t(1) << 32) + s.length() + 1);
    } else {
      // When a string is interned for the first time, and for some reason
      // more than one thread tries to intern it, then a leak might occur
      // with worst size of (threadCount - 1)*(stringLength + 1) bytes.
      //
      // insertPair.second would return false, if the same string location
      // was already inserted, allowing us to track the precise amount of
      // leaks - their count, and precise amount of bytes lost.
      //
      // The effect would be worse, with larger strings, amplified
      // by the amount of threads trying to intern it at the same time.
      //
      // In practice, this effect might be neglible, though an artifical
      // can be devised for worst case scenario.
      m_leakCountAndSizeStats.fetch_and((uint64_t(1) << 32) + s.length() + 1);
    }

    return *insertPair.first;
  }

  const char* Data() const { return m_poolData.get(); }
  uint32_t Size() const { return m_poolSize; }
  uint32_t Used() const { return m_poolUsed.load(); }

  std::pair<uint32_t, uint32_t> UsedStats() const {
      uint64_t usedCountAndSize = m_usedCountAndSizeStats.load();
      return std::make_pair( uint32_t(usedCountAndSize >> 32), uint32_t(usedCountAndSize & 0xFFFFFFFF)) ;
  }

  std::pair<uint32_t, uint32_t> LeakStats() const {
      uint64_t leakCountAndSize = m_leakCountAndSizeStats.load();
      return std::make_pair( uint32_t(leakCountAndSize >> 32), uint32_t(leakCountAndSize & 0xFFFFFFFF)) ;
  }

  // Note: the count and size may be off sync one to another, as they are kept in separate atomics.
  // TODO: Use 128-bit atomic to keep ref counter, and total size together.
  std::pair<uint64_t, uint64_t> RefStats() const {
      return std::make_pair( m_refCountStats.load(), m_refSizeStats.load() );
  }

};

int main(int argc, const char *argv[]) {
  size_t poolSize = 1024 * 1024;
  auto poolData = std::unique_ptr<char[]>(new char[1024 * 1024]);
  StringPool pool(poolSize, std::move(poolData));
  printf("pool data=%p, size=%u, used=%u\n", pool.Data(), pool.Size(), pool.Used());
  for (int i = 0; i < 10; i++) {
    const auto sv = pool.InternString("test");
    printf("%p %lu\n", sv.data(), sv.length());
  }
  printf("pool data=%p, size=%u, used=%u\n", pool.Data(), pool.Size(), pool.Used());
  return 0;
}
