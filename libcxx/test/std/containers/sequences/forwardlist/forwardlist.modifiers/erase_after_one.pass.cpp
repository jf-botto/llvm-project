//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// <forward_list>

// iterator erase_after(const_iterator p); // constexpr since C++26

#include <forward_list>
#include <cassert>
#include <iterator>

#include "test_macros.h"
#include "min_allocator.h"

TEST_CONSTEXPR_CXX26 bool test() {
  {
    typedef int T;
    typedef std::forward_list<T> C;
    const T t[] = {0, 1, 2, 3, 4};
    C c(std::begin(t), std::end(t));

    C::iterator i = c.erase_after(std::next(c.cbefore_begin(), 4));
    assert(i == c.end());
    assert(std::distance(c.begin(), c.end()) == 4);
    assert(*std::next(c.begin(), 0) == 0);
    assert(*std::next(c.begin(), 1) == 1);
    assert(*std::next(c.begin(), 2) == 2);
    assert(*std::next(c.begin(), 3) == 3);

    i = c.erase_after(std::next(c.cbefore_begin(), 0));
    assert(i == c.begin());
    assert(std::distance(c.begin(), c.end()) == 3);
    assert(*std::next(c.begin(), 0) == 1);
    assert(*std::next(c.begin(), 1) == 2);
    assert(*std::next(c.begin(), 2) == 3);

    i = c.erase_after(std::next(c.cbefore_begin(), 1));
    assert(i == std::next(c.begin()));
    assert(std::distance(c.begin(), c.end()) == 2);
    assert(*std::next(c.begin(), 0) == 1);
    assert(*std::next(c.begin(), 1) == 3);

    i = c.erase_after(std::next(c.cbefore_begin(), 1));
    assert(i == c.end());
    assert(std::distance(c.begin(), c.end()) == 1);
    assert(*std::next(c.begin(), 0) == 1);

    i = c.erase_after(std::next(c.cbefore_begin(), 0));
    assert(i == c.begin());
    assert(i == c.end());
    assert(std::distance(c.begin(), c.end()) == 0);
  }
#if TEST_STD_VER >= 11
  {
    typedef int T;
    typedef std::forward_list<T, min_allocator<T>> C;
    const T t[] = {0, 1, 2, 3, 4};
    C c(std::begin(t), std::end(t));

    C::iterator i = c.erase_after(std::next(c.cbefore_begin(), 4));
    assert(i == c.end());
    assert(std::distance(c.begin(), c.end()) == 4);
    assert(*std::next(c.begin(), 0) == 0);
    assert(*std::next(c.begin(), 1) == 1);
    assert(*std::next(c.begin(), 2) == 2);
    assert(*std::next(c.begin(), 3) == 3);

    i = c.erase_after(std::next(c.cbefore_begin(), 0));
    assert(i == c.begin());
    assert(std::distance(c.begin(), c.end()) == 3);
    assert(*std::next(c.begin(), 0) == 1);
    assert(*std::next(c.begin(), 1) == 2);
    assert(*std::next(c.begin(), 2) == 3);

    i = c.erase_after(std::next(c.cbefore_begin(), 1));
    assert(i == std::next(c.begin()));
    assert(std::distance(c.begin(), c.end()) == 2);
    assert(*std::next(c.begin(), 0) == 1);
    assert(*std::next(c.begin(), 1) == 3);

    i = c.erase_after(std::next(c.cbefore_begin(), 1));
    assert(i == c.end());
    assert(std::distance(c.begin(), c.end()) == 1);
    assert(*std::next(c.begin(), 0) == 1);

    i = c.erase_after(std::next(c.cbefore_begin(), 0));
    assert(i == c.begin());
    assert(i == c.end());
    assert(std::distance(c.begin(), c.end()) == 0);
  }
#endif

  return true;
}

int main(int, char**) {
  assert(test());
#if TEST_STD_VER >= 26
  static_assert(test());
#endif

  return 0;
}
