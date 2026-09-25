#include "aim/common/lazy_cache.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace aim;

using ::testing::Eq;
using ::testing::Optional;

TEST(LazyCacheTest, TestCacheSingleItem) {
  LazyCache<std::string> cache;

  LazyCacheOptions opts;
  opts.cache_stale_time_micros = 10;
  opts.num_to_load = 1;

  EXPECT_THAT(cache.Get("a"), Eq(std::nullopt));

  int counter = 1;
  auto load_fn = [&](const std::string& key) { return std::format("value-{}-{}", key, counter++); };

  opts.explicit_now_micros = 1;
  cache.LoadSomeItems(opts, load_fn);

  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-1")));
  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-1")));

  opts.explicit_now_micros += 9;
  cache.LoadSomeItems(opts, load_fn);

  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-1")));

  opts.explicit_now_micros += 2;
  cache.LoadSomeItems(opts, load_fn);

  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-2")));

  opts.cache_stale_time_micros = 0;
  cache.LoadSomeItems(opts, load_fn);

  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-3")));
  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-3")));
}

TEST(LazyCacheTest, TestCacheMultipleItems) {
  LazyCache<std::string> cache;

  LazyCacheOptions opts;
  opts.cache_stale_time_micros = 10;
  opts.num_to_load = 1;

  EXPECT_THAT(cache.Get("a"), Eq(std::nullopt));
  EXPECT_THAT(cache.Get("b"), Eq(std::nullopt));
  EXPECT_THAT(cache.Get("c"), Eq(std::nullopt));

  int counter = 1;
  auto load_fn = [&](const std::string& key) { return std::format("value-{}-{}", key, counter++); };

  opts.explicit_now_micros = 1;
  cache.LoadSomeItems(opts, load_fn);

  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-1")));
  EXPECT_THAT(cache.Get("b"), Eq(std::nullopt));
  EXPECT_THAT(cache.Get("c"), Eq(std::nullopt));
  EXPECT_THAT(cache.Get("d"), Eq(std::nullopt));

  cache.LoadSomeItems(opts, load_fn);

  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-1")));
  EXPECT_THAT(cache.Get("b"), Optional(Eq("value-b-2")));
  EXPECT_THAT(cache.Get("c"), Eq(std::nullopt));
  EXPECT_THAT(cache.Get("d"), Eq(std::nullopt));

  opts.explicit_now_micros += 12;
  cache.LoadSomeItems(opts, load_fn);

  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-1")));
  EXPECT_THAT(cache.Get("b"), Optional(Eq("value-b-2")));
  EXPECT_THAT(cache.Get("c"), Optional(Eq("value-c-3")));
  EXPECT_THAT(cache.Get("d"), Eq(std::nullopt));

  opts.num_to_load = 2;
  cache.LoadSomeItems(opts, load_fn);

  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-5")));
  EXPECT_THAT(cache.Get("b"), Optional(Eq("value-b-2")));
  EXPECT_THAT(cache.Get("c"), Optional(Eq("value-c-3")));
  EXPECT_THAT(cache.Get("d"), Optional(Eq("value-d-4")));

  opts.num_to_load = 20;
  cache.LoadSomeItems(opts, load_fn);

  EXPECT_THAT(cache.Get("a"), Optional(Eq("value-a-5")));
  EXPECT_THAT(cache.Get("b"), Optional(Eq("value-b-6")));
  EXPECT_THAT(cache.Get("c"), Optional(Eq("value-c-3")));
  EXPECT_THAT(cache.Get("d"), Optional(Eq("value-d-4")));
}
