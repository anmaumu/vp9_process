/**
 * @file cpu_vp9_decoder_threads_test.cpp
 * @brief Regression tests for bounded automatic VP9 decoder threading.
 */
#include "cpu_vp9_decoder_threads.hpp"

#include <cassert>

int main() {
    using mkvc::cpu_vp9_decoder_detail::resolve_thread_count;

    assert(resolve_thread_count(3, 48) == 3);
    assert(resolve_thread_count(0, 0) == 1);
    assert(resolve_thread_count(0, 1) == 1);
    assert(resolve_thread_count(0, 8) == 8);
    assert(resolve_thread_count(0, 16) == 16);
    assert(resolve_thread_count(0, 48) == 16);
    return 0;
}
