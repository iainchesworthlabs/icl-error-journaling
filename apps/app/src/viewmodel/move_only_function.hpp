// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace ej::viewmodel
{

template <typename Signature>
class MoveOnlyFunction;

/// The callable type of the view-model layer: a function object that can own a move-only capture (a `std::unique_ptr`,
/// a result slot). It stands in for `std::move_only_function`, which Apple's libc++ does not have before version 22
/// (the macOS legs of CI showed it); when every standard library of the build matrix has it, this class is the one
/// place to change. It supports what the layer needs and no more: non-const call, empty state, move only. Calling an
/// empty one is a mistake, as for the standard type.
template <typename Result, typename... Arguments>
class MoveOnlyFunction<Result(Arguments...)>
{
public:
    MoveOnlyFunction() = default;
    MoveOnlyFunction(std::nullptr_t) {}

    template <typename Callable>
        requires(!std::is_same_v<std::remove_cvref_t<Callable>, MoveOnlyFunction> &&
                 std::is_invocable_r_v<Result, std::decay_t<Callable>&, Arguments...>)
    MoveOnlyFunction(Callable&& callable)
        : m_Callable(std::make_unique<Model<std::decay_t<Callable>>>(std::forward<Callable>(callable)))
    {
    }

    MoveOnlyFunction(MoveOnlyFunction&&) noexcept = default;
    MoveOnlyFunction& operator=(MoveOnlyFunction&&) noexcept = default;
    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;
    ~MoveOnlyFunction() = default;

    Result operator()(Arguments... arguments)
    {
        return m_Callable->call(std::forward<Arguments>(arguments)...);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_Callable != nullptr;
    }

private:
    struct Concept
    {
        virtual ~Concept() = default;
        virtual Result call(Arguments... arguments) = 0;
    };

    template <typename Callable>
    struct Model final : Concept
    {
        template <typename Argument>
        explicit Model(Argument&& callable) : m_Function(std::forward<Argument>(callable))
        {
        }

        Result call(Arguments... arguments) override
        {
            return std::invoke_r<Result>(m_Function, std::forward<Arguments>(arguments)...);
        }

        Callable m_Function;
    };

    std::unique_ptr<Concept> m_Callable;
};

} // namespace ej::viewmodel
