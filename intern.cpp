#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS 1
#endif

#include <atomic>
#include <execution>
#include <memory>
#include <stdio.h>
#include <string>
#include <memory_resource>
#include <iterator>
#include <mutex>
#include <set>

#ifdef _MSC_VER
#include <windows.h>
#include <memoryapi.h>
#include <concurrent_unordered_set.h>
using concurrency::concurrent_unordered_set;
#else
#include <tbb/concurrent_unordered_set.h>
using tbb::concurrent_unordered_set;
#endif

class StringPool {
  concurrent_unordered_set<std::string_view, std::hash<std::string_view>, std::equal_to<std::string_view> /*, std::pmr::polymorphic_allocator<std::string_view>*/>
      m_storage;

  struct Page {
    explicit Page( size_t size )
        : m_size(size) {
      m_data = (char*)malloc( m_size );
      m_used.store(0);
      m_usedCountAndSizeStats.store(0);
      m_leakCountAndSizeStats.store(0);
      m_refCountStats.store(0);
      m_refSizeStats.store(0);
      m_failCountStats.store(0);
      m_failSizeStats.store(0);
    }
    size_t m_size{ 0 };
    char* m_data{ nullptr };
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> m_used;
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> m_usedCountAndSizeStats;
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> m_leakCountAndSizeStats;
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> m_refCountStats;
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> m_failCountStats;
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> m_refSizeStats;
    alignas(std::hardware_destructive_interference_size) std::atomic<uint64_t> m_failSizeStats;

    // Places a string in the next availabe position into the data pool.
    // If there is not enough space, returns nullptr,
    // otherwise returns pointer to the location where the string was copied.
    //
    // The operation is threadsafe, using an atomic index for pool allocation.
    __declspec(noinline)
    const char *Alloc(std::string_view s) {
      const auto stringLength = s.length();
      const auto dataLength = stringLength + 1;
      const auto dataOffset = m_used.fetch_add(dataLength);
      if (dataOffset + dataLength >= m_size) {
        m_used = m_size;
        return nullptr;
      }
      auto data = m_data + dataOffset;
      memcpy(data, s.data(), stringLength);
      data[stringLength] = 0;
      return data;
    }
  };

  std::condition_variable m_cv;
  size_t m_pageSize{ 0 };
  std::mutex m_pageAllocateMutex;
  alignas(std::hardware_destructive_interference_size) std::atomic<Page*> m_page{ nullptr };
  concurrent_unordered_set<Page*> m_pages;

  // Ensure that page with bytesNeeded exist. It's best effort, since another thread might take over.
  Page* EnsurePageBytes( size_t bytesNeeded )
  {
    auto page = m_page.load();
    for( ;; )
    {
      // Try the current page.
      if( page->m_used + bytesNeeded < page->m_size )
        return page;

      for( ;; ) 
      {
        Page* oldPage = nullptr;
        Page* newPage = nullptr;
        {
          std::scoped_lock<std::mutex> lk( m_pageAllocateMutex );
          auto pageAfterLock = m_page.load();
          if( pageAfterLock != page )
          {
            page = pageAfterLock;
            break;
          }

          newPage = new Page( m_pageSize );
          oldPage = m_page.exchange( newPage );
        }
        if( oldPage )
        {
          m_pages.insert( oldPage );
          printf( "Old page %p, used=%zu\n", oldPage, oldPage->m_used.load() );
        }
        return newPage;
      }
    }
  }

public:
  explicit StringPool(size_t pageSize)
      : m_pageSize( pageSize )
  {
    m_page = new Page( m_pageSize );
  }

  std::string_view Intern(std::string_view s) {
    const auto it = m_storage.find(s);
    if (it != m_storage.cend())
      return *it;

    Page* page = nullptr;
    const char* pooledString = nullptr;
    do {
      // Expect page that can fit s.length() bytes (best effort).
      page = EnsurePageBytes( s.length() );
      pooledString = page->Alloc(s);
    } while( nullptr == pooledString );

    const auto insertPair = m_storage.insert({pooledString, s.length()});
    if (insertPair.second) {
      page->m_usedCountAndSizeStats.fetch_add((uint64_t(1) << 32) +
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
      page->m_leakCountAndSizeStats.fetch_add((uint64_t(1) << 32) +
                                                s.length() + 1);
    }

    return *insertPair.first;
  }

  std::set<Page*> AllPages() const
  {
    std::set<Page*> pages;
    for( const auto page : m_pages )
      pages.insert( page );
    pages.insert( m_page.load() );
    return pages;
  }

  const char *Data() const { return m_page.load()->m_data; }
  size_t Size() const { return m_page.load()->m_size; }
  size_t Used() const { return m_page.load()->m_used.load(); }

  std::pair<uint32_t, uint32_t> UsedStats() const {
    uint64_t usedCountAndSize{ 0 };
    for( const auto page : AllPages() )
      usedCountAndSize += page->m_usedCountAndSizeStats.load();
    return std::make_pair(uint32_t(usedCountAndSize >> 32),
                          uint32_t(usedCountAndSize & 0xFFFFFFFF));
  }

  std::pair<uint32_t, uint32_t> LeakStats() const {
    uint64_t leakCountAndSize{ 0 };
    for( const auto page : AllPages() )
      leakCountAndSize += page->m_leakCountAndSizeStats.load();
    return std::make_pair(uint32_t(leakCountAndSize >> 32),
                          uint32_t(leakCountAndSize & 0xFFFFFFFF));
  }

  // Note: the count and size may be off sync one to another, as they are kept
  // in separate atomics.
  // TODO: Use 128-bit atomic to keep ref counter, and total size together.
  std::pair<uint64_t, uint64_t> RefStats() const {
    const auto page = m_page.load();
    return std::make_pair(page->m_refCountStats.load(),
                          page->m_refSizeStats.load());
  }

  std::pair<uint64_t, uint64_t> FailStats() const {
    const auto page = m_page.load();
    return std::make_pair(page->m_failCountStats.load(),
                          page->m_failSizeStats.load());
  }
};

void PrintStats(const StringPool &pool) {
  auto refStats = pool.RefStats();
  auto usedStats = pool.UsedStats();
  auto leakStats = pool.LeakStats();
  auto failStats = pool.FailStats();
  printf("size=%zu used=%zu, ref=%zu/%zu used=%zu/%zu leaks=%zu/%zu fails=%zu/%zu\n",
         (size_t)pool.Size(), (size_t)pool.Used(), (size_t)refStats.first, (size_t)refStats.second,
         (size_t)usedStats.first, (size_t)usedStats.second, (size_t)leakStats.first, (size_t)leakStats.second,
         (size_t)failStats.first, (size_t)failStats.second);
}

int main(int argc, const char *argv[]) {
  size_t poolSize = 1024*1024; //1024*1024;
  std::vector<int> p;
  p.resize(999983 );
  for (int i = 0; i < p.size(); i++)
    p[i] = i;

  StringPool pool(poolSize);

  printf("inited...\n");
  pool.Intern("one");
  pool.Intern("two");
  pool.Intern("three");
  pool.Intern("one");
  pool.Intern("one");
  pool.Intern("one");

  int primes[] = {1, 2, 3, 5, 7, 59, 97, 229, 379, 541};
  for (int pi = 0; pi < std::size(primes); pi++) {
    auto t0 = std::chrono::steady_clock::now();
    int prime = primes[std::size(primes) - pi - 1];
    //int prime = primes[pi];
    std::vector<std::pair<std::string, int>> strings;
    strings.resize(p.size());
    std::for_each(std::execution::par, strings.begin(), strings.end(), [&](std::pair<std::string, int>& p) {
      char buf[4096];
      int idx = int(&p - &strings[0]);
      sprintf(buf, 
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
      /*
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        "testtesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttesttest"
        */
            "test%d%d",
              pi, idx / 10);
      p.first.assign(buf);
      p.second = (idx / prime) % strings.size();
    });
    auto t1 = std::chrono::steady_clock::now();
    printf( "time=%.4f ", (t1-t0).count() / 1e9);
    for(int steps=0;steps<1024;steps++)
    std::for_each(std::execution::par, p.begin(), p.end(), [&](int& d) {
      const auto& p = strings[d];
      const auto sv0 = pool.Intern(strings[p.second].first);
    });
    auto t2 = std::chrono::steady_clock::now();
    printf( "time=%.4f ", (t2-t1).count() / 1e9);
    PrintStats(pool);
  }
  return 0;
}
