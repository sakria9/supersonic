#define BOOST_TEST_MODULE SuperSonicTest
#include <boost/test/included/unit_test.hpp>  //single-header

#include "utils.h"

BOOST_AUTO_TEST_CASE(np_test) {
  using namespace SuperSonic::Signal;
  using Vec = std::vector<float>;

  auto check_float_eq = [](float a, float b) {
    BOOST_CHECK(fabsf(a - b) < 1e-6);
  };

  {
    auto v = linspace(0, 1, 10);
    // 0, 0.1, ..., 0.9
    BOOST_CHECK_EQUAL(v.size(), 10);
    for (int i = 0; i < 10; i++) {
      check_float_eq(v[i], i * 0.1f);
    }
  }
  {
    // concatenate
    Vec a{1, 2, 3}, b{4, 5, 6}, c{7, 8, 9};
    auto v = concatenate(a, b, c);
    BOOST_CHECK_EQUAL(v.size(), 9);
    for (int i = 0; i < 9; i++) {
      BOOST_CHECK_EQUAL(v[i], i + 1);
    }
  }
  {
    // flip
    Vec a{1, 2, 3};
    auto v = flip(a);
    BOOST_CHECK_EQUAL(v.size(), 3);
    BOOST_CHECK_EQUAL(v[0], 3);
    BOOST_CHECK_EQUAL(v[1], 2);
    BOOST_CHECK_EQUAL(v[2], 1);
  }
  {
    // zeros
    auto v = zeros<int>(10);
    BOOST_CHECK_EQUAL(v.size(), 10);
    for (int i = 0; i < 10; i++) {
      BOOST_CHECK_EQUAL(v[i], 0);
    }
  }
  {
    // dot
    Vec a{1, 2, 3}, b{4, 5, 6};
    check_float_eq(dot(a, b), 1 * 4 + 2 * 5 + 3 * 6);
  }
  {
    // absv
    Vec a{1, -2, 3};
    auto v = absv(a);
    BOOST_CHECK_EQUAL(v.size(), 3);
    BOOST_CHECK_EQUAL(v[0], 1);
    BOOST_CHECK_EQUAL(v[1], 2);
    BOOST_CHECK_EQUAL(v[2], 3);
  }
  {
    // maxv
    Vec a{1, 2, 3};
    BOOST_CHECK_EQUAL(maxv(a), 3);
  }
  {
    // argmax
    Vec a{1, 2, 3};
    BOOST_CHECK_EQUAL(argmax(a), 2);
  }
}