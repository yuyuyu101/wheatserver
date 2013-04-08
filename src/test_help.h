// This is a really minimal testing framework for C. Learn from
// [MinUnit](http://www.jera.com/techinfo/jtns/jtn002.html)
//
// Example:
//
// test_cond("Check if 1 == 1", 1==1)
// test_cond("Check if 5 > 10", 5 > 10)
// test_report()
//
// ----------------------------------------------------------------------------
// Copyright (c) 2013 The Wheatserver Author. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef WHEATSERVER_TEST_HELP_H
#define WHEATSERVER_TEST_HELP_H

// In Wheatserver, use this unit test framework to test base data structure
// like list, dict, wstr, slice, mbuf.
// You can find using example in these implementation file

int __failed_tests = 0;
int __test_num = 0;

#define test_cond(descr,_c) do { \
    __test_num++; printf("%d - %s: ", __test_num, descr); \
    if(_c) printf("PASSED\n"); else {printf("FAILED\n"); __failed_tests++;} \
} while(0)

#define test_report() do { \
    printf("%d tests, %d passed, %d failed\n", __test_num, \
                    __test_num-__failed_tests, __failed_tests); \
    if (__failed_tests) { \
        printf("=== WARNING === We have failed tests here...\n"); \
        exit(1); \
    } \
} while(0)

#endif
