#include <atomic>
#include <execution>
#include <memory>
#include <stdio.h>
#include <string>

#ifdef _MSC_VER
#include <concurrent_unordered_set.h>
using concurrency::concurrent_unordered_set;
#else
#include <tbb/concurrent_unordered_set.h>
using tbb::concurrent_unordered_set;
#endif

class StringPool {
  concurrent_unordered_set<std::string_view, std::hash<std::string_view>>
      m_storage;

  struct Page {
    explicit Page(uint32_t size, std::unique_ptr<char[]> &&pool)
        : m_size(size), m_data(std::move(pool)) {
      m_used.store(0);
      m_usedCountAndSizeStats.store(0);
      m_leakCountAndSizeStats.store(0);
      m_refCountStats.store(0);
      m_refSizeStats.store(0);
      m_failCountStats.store(0);
      m_failSizeStats.store(0);
    }
    std::atomic<uint64_t> m_used;
    uint64_t m_size;
    std::unique_ptr<char[]> m_data;

    std::atomic<uint64_t> m_usedCountAndSizeStats;
    std::atomic<uint64_t> m_leakCountAndSizeStats;
    std::atomic<uint64_t> m_refCountStats;
    std::atomic<uint64_t> m_failCountStats;
    std::atomic<uint64_t> m_refSizeStats;
    std::atomic<uint64_t> m_failSizeStats;

    // Places a string in the next availabe position into the data pool.
    // If there is not enough space, returns nullptr,
    // otherwise returns pointer to the location where the string was copied.
    //
    // The operation is threadsafe, using an atomic index for pool allocation.
    const char *Alloc(std::string_view s) {
      const auto stringLength = s.length();
      const auto dataLength = stringLength + 1;
      const auto dataOffset = m_used.fetch_add(dataLength);
      if (dataOffset + dataLength >= m_size) {
        m_used = m_size;
        return nullptr;
      }
      auto data = m_data.get() + dataOffset;
      memcpy(data, s.data(), stringLength);
      data[stringLength] = 0;
      return data;
    }
  };

  Page m_onePage;
  Page *m_page;

public:
  explicit StringPool(size_t size, std::unique_ptr<char[]> &&pool)
      : m_onePage(size, std::move(pool)), m_page(&m_onePage) {}

  std::string_view InternString(std::string_view s) {
    // Lookup the input string_view, and if found
    // return the pooled one.
    printf("str=[%.*s]\n", s.length(), s.data());
    const auto it = m_storage.find(s);
    if (it != m_storage.cend()) {
      // Increase the number of times any string has been referenced more than
      // once.
      m_page->m_refCountStats.fetch_add(1);
      m_page->m_refSizeStats.fetch_add(s.length() + 1);
      return *it;
    }

    // If not, try to pool the string, if there is space.
    // If there is no space, return nullptr.
    // NOTE: Trying to pool the same string twice from
    // different threads would result in racing condition
    // but one of them is getting in. At worst, the loss
    // is the N duplicates, where N is the number of cpus.
    const auto pooledString = m_page->Alloc(s);
    if (!pooledString) {
      m_page->m_failCountStats.fetch_add(1);
      m_page->m_failSizeStats.fetch_add(s.length() + 1);
      return std::string_view();
    }

    const auto insertPair = m_storage.insert({pooledString, s.length()});
    if (insertPair.second) {
      m_page->m_usedCountAndSizeStats.fetch_add((uint64_t(1) << 32) +
                                                s.length() + 1);
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
      m_page->m_leakCountAndSizeStats.fetch_add((uint64_t(1) << 32) +
                                                s.length() + 1);
    }

    return *insertPair.first;
  }

  const char *Data() const { return m_page->m_data.get(); }
  uint32_t Size() const { return m_page->m_size; }
  uint32_t Used() const { return m_page->m_used.load(); }

  std::pair<uint32_t, uint32_t> UsedStats() const {
    uint64_t usedCountAndSize = m_page->m_usedCountAndSizeStats.load();
    return std::make_pair(uint32_t(usedCountAndSize >> 32),
                          uint32_t(usedCountAndSize & 0xFFFFFFFF));
  }

  std::pair<uint32_t, uint32_t> LeakStats() const {
    uint64_t leakCountAndSize = m_page->m_leakCountAndSizeStats.load();
    return std::make_pair(uint32_t(leakCountAndSize >> 32),
                          uint32_t(leakCountAndSize & 0xFFFFFFFF));
  }

  // Note: the count and size may be off sync one to another, as they are kept
  // in separate atomics.
  // TODO: Use 128-bit atomic to keep ref counter, and total size together.
  std::pair<uint64_t, uint64_t> RefStats() const {
    return std::make_pair(m_page->m_refCountStats.load(),
                          m_page->m_refSizeStats.load());
  }

  std::pair<uint64_t, uint64_t> FailStats() const {
    return std::make_pair(m_page->m_failCountStats.load(),
                          m_page->m_failSizeStats.load());
  }
};

void PrintStats(const StringPool &pool) {
  auto refStats = pool.RefStats();
  auto usedStats = pool.UsedStats();
  auto leakStats = pool.LeakStats();
  auto failStats = pool.FailStats();
  printf("size=%u used=%u, ref=%lu/%lu used=%u/%u leaks=%u/%u fails=%u/%u\n",
         pool.Size(), pool.Used(), refStats.first, refStats.second,
         usedStats.first, usedStats.second, leakStats.first, leakStats.second,
         failStats.first, failStats.second);
}

int main(int argc, const char *argv[]) {
  size_t poolSize = 1024 * 1024 * 256;
  std::vector<int> p;
  p.resize(1024 * 1024 * 16);
  for (int i = 0; i < p.size(); i++)
    p[i] = i;
  auto poolData = std::unique_ptr<char[]>(new char[poolSize]);
  StringPool pool(poolSize, std::move(poolData));
  PrintStats(pool);
  for (int i = 0; i < 10; i++) {
    const auto sv = pool.InternString("test");
    //    printf("%p %lu\n", sv.data(), sv.length());
  }
  PrintStats(pool);
  PrintStats(pool);
  int primes[] = {1, 2, 3, 5, 7, 59, 97, 229, 379, 541};
  for (int pi = 0; pi < std::size(primes); pi++) {
    int prime = primes[std::size(primes) - pi - 1];
    std::vector<std::string> strings;
    strings.resize(15485867);
    for (int i = 0; i < strings.size(); i++) {
      static char buf[1024*1024*16];
      sprintf(buf,
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
              "testtesttesttesttesttest%d%d",
              pi, i / 10);
      strings[i] = buf;
//      printf("len=%d\n", strlen(buf));
    }
    std::for_each(std::execution::par_unseq, p.begin(), p.end(), [&](int &d) {
      const auto sv0 = pool.InternString(strings[(d / prime) % strings.size()]);
    });
    PrintStats(pool);
  }
  return 0;
}
