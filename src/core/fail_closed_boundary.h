#pragma once

#include <exception>
#include <utility>

namespace ff7r::piano::core {

enum class FailClosedException {
    Standard,
    Unknown,
};

template <typename Result, typename Body, typename FailureHandler>
Result invoke_fail_closed(
    Body&& body,
    FailureHandler&& failure_handler,
    const Result failure_result) noexcept
{
    try {
        return std::forward<Body>(body)();
    } catch (const std::exception&) {
        try {
            std::forward<FailureHandler>(failure_handler)(FailClosedException::Standard);
        } catch (...) {
        }
    } catch (...) {
        try {
            std::forward<FailureHandler>(failure_handler)(FailClosedException::Unknown);
        } catch (...) {
        }
    }
    return failure_result;
}

} // namespace ff7r::piano::core
