#include "stdafx.h"
#include "framework.h"

#include <core/misc/common.h>

#include <core/concurrency/coroutine.h>

namespace NYT {
namespace NConcurrency {
namespace {

////////////////////////////////////////////////////////////////////////////////

void Coroutine0(TCoroutine<int()>& self)
{
    self.Yield(1);
    self.Yield(2);
    self.Yield(3);
    self.Yield(4);
    self.Yield(5);
}

TEST(TCoroutineTest, Nullary)
{
    TCoroutine<int()> coro(BIND(&Coroutine0));
    EXPECT_EQ(EFiberState::Initialized, coro.GetState());

    int i;
    TNullable<int> actual;
    for (i = 1; /**/; ++i) {
        actual = coro.Run();
        if (coro.GetState() == EFiberState::Terminated) {
            break;
        }
        EXPECT_TRUE(actual.HasValue());
        EXPECT_EQ(i, actual.Get());
    }

    EXPECT_FALSE(actual.HasValue());
    EXPECT_EQ(6, i);

    EXPECT_EQ(EFiberState::Terminated, coro.GetState());
}

void Coroutine1(TCoroutine<int(int)>& self, int arg)
{
    EXPECT_EQ(0, arg);
    std::tie(arg) = self.Yield(arg + 1);
    EXPECT_EQ(2, arg);
    std::tie(arg) = self.Yield(arg + 1);
    EXPECT_EQ(4, arg);
    std::tie(arg) = self.Yield(arg + 1);
    EXPECT_EQ(6, arg);
    std::tie(arg) = self.Yield(arg + 1);
    EXPECT_EQ(8, arg);
    std::tie(arg) = self.Yield(arg + 1);
    EXPECT_EQ(10, arg);
}

TEST(TCoroutineTest, Unary)
{
    TCoroutine<int(int)> coro(BIND(&Coroutine1));
    EXPECT_EQ(EFiberState::Initialized, coro.GetState());

    // Alternative syntax.
    int i = 0, j = 0;
    TNullable<int> actual;
    while ((actual = coro.Run(j))) {
        ++i;
        EXPECT_EQ(i * 2 - 1, actual.Get());
        EXPECT_EQ(i * 2 - 2, j);
        j = actual.Get() + 1;
    }

    EXPECT_FALSE(actual.HasValue());
    EXPECT_EQ(5, i);
    EXPECT_EQ(10, j);
}

// In this case I've got lazy and set up these test cases.
struct { int lhs; int rhs; int sum; } Coroutine2TestCases[] = {
    { 10, 20, 30 },
    { 11, 21, 32 },
    { 12, 22, 34 },
    { 13, 23, 36 },
    { 14, 24, 38 },
    { 15, 25, 40 }
};

void Coroutine2(TCoroutine<int(int, int)>& self, int lhs, int rhs)
{
    for (int i = 0; i < ARRAY_SIZE(Coroutine2TestCases); ++i) {
        EXPECT_EQ(Coroutine2TestCases[i].lhs, lhs) << "Iteration #" << i;
        EXPECT_EQ(Coroutine2TestCases[i].rhs, rhs) << "Iteration #" << i;
        std::tie(lhs, rhs) = self.Yield(lhs + rhs);
    }
}

TEST(TCoroutineTest, Binary)
{
    TCoroutine<int(int, int)> coro(BIND(&Coroutine2));
    EXPECT_EQ(EFiberState::Initialized, coro.GetState());

    int i = 0;
    TNullable<int> actual;
    for (
        i = 0;
        (actual = coro.Run(
            Coroutine2TestCases[i].lhs,
            Coroutine2TestCases[i].rhs));
        ++i
    ) {
        EXPECT_EQ(Coroutine2TestCases[i].sum, actual.Get());
    }

    EXPECT_FALSE(actual.HasValue());
    EXPECT_EQ(i, ARRAY_SIZE(Coroutine2TestCases));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NConcurrency
} // namespace NYT

