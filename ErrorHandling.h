#pragma once

#include <stdexcept>
#include <cassert>

#define STRINGIZE_IMPL(x) #x
#define STRINGIZE(x) STRINGIZE_IMPL(x)

#define CONCAT_IMPL(x, y) x ## y
#define CONCAT(x, y) CONCAT_IMPL(x, y)

#define FILE_LINE CONCAT(__FILE__": ", STRINGIZE(__LINE__))

#define ASSERT(x) assert(x)

#define CHECK(x) if (!(x)) { throw std::runtime_error("Check failed at "FILE_LINE); }

#define EXPECT(x) if (!(x)) { ASSERT(x); throw std::runtime_error("Expect failed at "FILE_LINE); }
#define REQUIRE(x) ASSERT(x)
#define INVARIANT(x) ASSERT(x)