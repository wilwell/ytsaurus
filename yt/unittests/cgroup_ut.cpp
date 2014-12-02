#include "stdafx.h"

#include "framework.h"

#include <ytlib/cgroup/cgroup.h>
#include <ytlib/cgroup/event.h>

#include <core/misc/proc.h>
#include <core/misc/fs.h>

#ifdef _linux_
  #include <sys/wait.h>
  #include <unistd.h>
  #include <sys/eventfd.h>
#endif

#include <array>

namespace NYT {
namespace NCGroup {
namespace {

////////////////////////////////////////////////////////////////////////////////

template <typename T>
T CreateCGroup(const Stroka& name)
{
    T group(name);
    group.Create();
    return group;
}

////////////////////////////////////////////////////////////////////////////////


TEST(CGroup, CreateDestroy)
{
    for (int i = 0; i < 2; ++i) {
        auto group = CreateCGroup<TBlockIO>("some");
        group.Destroy();
    }
}

#ifdef _linux_

TEST(CGroup, NotExistingGroupGetTasks)
{
    TBlockIO group("wierd_name");
    EXPECT_THROW(group.GetTasks(), std::exception);
}

#endif

TEST(CGroup, DoubleCreate)
{
    TBlockIO group("wierd_name");
    group.Create();
    group.Create();
    group.Destroy();
}

TEST(CGroup, EmptyHasNoTasks)
{
    auto group = CreateCGroup<TBlockIO>("some2");
    auto tasks = group.GetTasks();
    EXPECT_EQ(0, tasks.size());
    group.Destroy();
}

#ifdef _linux_

TEST(CGroup, AddCurrentTask)
{
    auto group = CreateCGroup<TBlockIO>("some");

    auto pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        group.AddCurrentTask();
        auto tasks = group.GetTasks();
        ASSERT_EQ(1, tasks.size());
        EXPECT_EQ(getpid(), tasks[0]);
        exit(0);
    }

    auto waitedpid = waitpid(pid, nullptr, 0);

    group.Destroy();

    ASSERT_EQ(pid, waitedpid);
}

TEST(CGroup, UnableToDestoryNotEmptyCGroup)
{
    auto group = CreateCGroup<TBlockIO>("some");

    auto addedEvent = eventfd(0, 0);
    auto triedRemoveEvent = eventfd(0, 0);

    auto pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        group.AddCurrentTask();

        i64 value = 1024;
        ASSERT_EQ(sizeof(value), ::write(addedEvent, &value, sizeof(value)));

        ASSERT_EQ(sizeof(value), ::read(triedRemoveEvent, &value, sizeof(value)));
        exit(0);
    }

    i64 value;
    ASSERT_EQ(sizeof(value), ::read(addedEvent, &value, sizeof(value)));
    EXPECT_THROW(group.Destroy(), std::exception);

    value = 1;
    ASSERT_EQ(sizeof(value), ::write(triedRemoveEvent, &value, sizeof(value)));

    auto waitedpid = waitpid(pid, nullptr, 0);

    group.Destroy();
    ASSERT_EQ(pid, waitedpid);

    ASSERT_EQ(0, close(addedEvent));
    ASSERT_EQ(0, close(triedRemoveEvent));
}

TEST(CGroup, DestroyAndGrandChildren)
{
    auto group = CreateCGroup<TBlockIO>("grandchildren");

    auto pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        group.AddCurrentTask();

        ASSERT_EQ(0, daemon(0, 0));

        exit(0);
    }

    ASSERT_EQ(pid , waitpid(pid, nullptr, 0));

    while (true) {
        auto pids = group.GetTasks();
        if (pids.empty()) {
            break;
        }
        for (auto pid: pids) {
            ASSERT_EQ(0, kill(pid, SIGTERM));
        }
    }

    group.Destroy();
}

TEST(CGroup, GetCpuAccStat)
{
    auto group = CreateCGroup<TCpuAccounting>("some");

    auto stats = group.GetStatistics();
    EXPECT_EQ(0, stats.UserTime.MilliSeconds());
    EXPECT_EQ(0, stats.SystemTime.MilliSeconds());

    group.Destroy();
}

TEST(CGroup, GetBlockIOStat)
{
    auto group = CreateCGroup<TBlockIO>("some");

    auto stats = group.GetStatistics();
    EXPECT_EQ(0, stats.BytesRead);
    EXPECT_EQ(0, stats.BytesWritten);

    group.Destroy();
}

TEST(CGroup, GetMemoryStats)
{
    auto group = CreateCGroup<TMemory>("some");

    auto stats = group.GetStatistics();
    EXPECT_EQ(0, group.GetUsageInBytes());
    EXPECT_EQ(0, stats.Rss);

    group.Destroy();
}

TEST(CGroup, UsageInBytesWithoutLimit)
{
    const i64 memoryUsage = 8 * 1024 * 1024;
    auto group = CreateCGroup<TMemory>("some");
    auto event = group.GetOomEvent();

    i64 num = 1;
    auto exitBarier = ::eventfd(0, 0);
    EXPECT_TRUE(exitBarier > 0);
    auto initBarier = ::eventfd(0, 0);
    EXPECT_TRUE(initBarier > 0);

    auto pid = fork();
    if (pid == 0) {
        group.AddCurrentTask();
        volatile char* data = new char[memoryUsage];
        for (int i = 0; i < memoryUsage; ++i) {
            data[i] = 0;
        }

        YCHECK(::write(initBarier, &num, sizeof(num)) == sizeof(num));
        YCHECK(::read(exitBarier, &num, sizeof(num)) == sizeof(num));

        delete[] data;
        _exit(1);
    }

    EXPECT_TRUE(::read(initBarier, &num, sizeof(num)) == sizeof(num));

    auto statistics = group.GetStatistics();
    EXPECT_TRUE(group.GetUsageInBytes() >= memoryUsage);
    EXPECT_TRUE(group.GetMaxUsageInBytes() >= memoryUsage);
    EXPECT_TRUE(statistics.Rss >= memoryUsage);

    EXPECT_TRUE(::write(exitBarier, &num, sizeof(num)) == sizeof(num));

    EXPECT_EQ(pid, waitpid(pid, nullptr, 0));
}

TEST(CGroup, OomEnabledByDefault)
{
    auto group = CreateCGroup<TMemory>("some");

    EXPECT_TRUE(group.IsOomEnabled());

    group.Destroy();
}

TEST(CGroup, DisableOom)
{
    auto group = CreateCGroup<TMemory>("some");
    group.DisableOom();

    EXPECT_FALSE(group.IsOomEnabled());

    group.Destroy();
}

TEST(CGroup, OomSettingsIsInherited)
{
    auto group = CreateCGroup<TMemory>("parent");
    group.DisableOom();

    auto child = CreateCGroup<TMemory>("parent/child");
    EXPECT_FALSE(child.IsOomEnabled());

    child.Destroy();
    group.Destroy();
}

TEST(CGroup, UnableToDisableOom)
{
    auto group = CreateCGroup<TMemory>("parent");
    group.EnableHierarchy();

    auto child = CreateCGroup<TMemory>("parent/child");
    EXPECT_THROW(group.DisableOom(), std::exception);

    child.Destroy();
    group.Destroy();
}

TEST(CGroup, GetOomEventIfOomIsEnabled)
{
    auto group = CreateCGroup<TMemory>("some");
    auto event = group.GetOomEvent();
}

TEST(CGroup, OomEventFiredIfOomIsEnabled)
{
    const i64 limit = 8 * 1024 * 1024;
    auto group = CreateCGroup<TMemory>("some");
    group.SetLimitInBytes(limit);
    auto event = group.GetOomEvent();

    auto pid = fork();
    if (pid == 0) {
        group.AddCurrentTask();
        volatile char* data = new char[limit + 1];
        for (int i = 0; i < limit + 1; ++i) {
            data[i] = 0;
        }
        delete[] data;
        _exit(1);
    }

    int status;
    auto waitedpid = waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status));

    EXPECT_TRUE(event.Fired());
    EXPECT_EQ(1, event.GetLastValue());

    group.Destroy();

    ASSERT_EQ(pid, waitedpid);
}

TEST(CGroup, OomEventMissingEvent)
{
    const i64 limit = 8 * 1024 * 1024;
    auto group = CreateCGroup<TMemory>("some");
    group.SetLimitInBytes(limit);

    auto pid = fork();
    if (pid == 0) {
        group.AddCurrentTask();
        volatile char* data = new char[limit + 1];
        for (int i = 0; i < limit; ++i) {
            data[i] = 0;
        }
        delete[] data;
        _exit(1);
    }

    int status;
    auto waitedpid = waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status));

    auto event = group.GetOomEvent();
    EXPECT_FALSE(event.Fired());

    group.Destroy();

    ASSERT_EQ(pid, waitedpid);
}

TEST(CGroup, ParentLimit)
{
    const i64 limit = 8 * 1024 * 1024;
    auto parent = CreateCGroup<TMemory>("parent");
    parent.EnableHierarchy();
    parent.SetLimitInBytes(limit);

    auto child = CreateCGroup<TMemory>("parent/child");
    auto childOom = child.GetOomEvent();

    auto pid = fork();
    if (pid == 0) {
        child.AddCurrentTask();
        volatile char* data = new char[limit + 1];
        for (int i = 0; i < limit; ++i) {
            data[i] = 0;
        }
        delete[] data;
        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status));

    EXPECT_TRUE(childOom.Fired());
}

TEST(CGroup, ParentLimitTwoChildren)
{
    const i64 limit = 8 * 1024 * 1024;
    auto parent = CreateCGroup<TMemory>("parent");
    parent.EnableHierarchy();
    parent.SetLimitInBytes(limit);
    TEvent parentOom = parent.GetOomEvent();

    auto exitBarier = ::eventfd(0, EFD_SEMAPHORE);
    EXPECT_TRUE(exitBarier > 0);

    auto initBarier = ::eventfd(0, EFD_SEMAPHORE);
    EXPECT_TRUE(initBarier > 0);

    std::array<TMemory, 2> children = {
        TMemory("parent/child"),
        TMemory("parent/other_child")
    };

    std::array<TEvent, 2> oomEvents;

    for (auto i = 0; i < children.size(); ++i) {
        children[i].Create();
        oomEvents[i] = children[i].GetOomEvent();
    }

    std::array<int, 2> pids;
    for (auto i = 0; i < children.size(); ++i) {
        pids[i] = fork();
        EXPECT_TRUE(pids[i] >= 0);

        if (pids[i] == 0) {
            children[i].AddCurrentTask();

            volatile char* data = new char[limit / 2 + 1];
            for (int i = 0; i < limit / 2; ++i) {
                data[i] = 0;
            }

            i64 num = 1;
            YCHECK(::write(initBarier, &num, sizeof(num)) == sizeof(num));

            YCHECK(::read(exitBarier, &num, sizeof(num)) == sizeof(num));
            delete[] data;
            _exit(1);
        }

        if (i == 0) {
            i64 num;
            EXPECT_EQ(sizeof(num), ::read(initBarier, &num, sizeof(num)));
        }
    }

    int status;
    auto pid = wait(&status);
    EXPECT_TRUE(WIFSIGNALED(status));

    i64 num;
    num = 2;
    EXPECT_EQ(sizeof(num), ::write(exitBarier, &num, sizeof(num)));

    int index;
    if (pids[0] == pid) {
        index = 0;
    } else {
        index = 1;
    }

    EXPECT_TRUE(oomEvents[index].Fired());
    EXPECT_TRUE(oomEvents[1 - index].Fired());
    EXPECT_TRUE(parentOom.Fired());

    EXPECT_TRUE(children[index].GetMaxUsageInBytes() < limit);

    EXPECT_EQ(pids[1 - index], waitpid(pids[1 - index], nullptr, 0));
}

// This test proves that there is a bug in memory cgroup
TEST(CGroup, Bug)
{
    char buffer[1024];

    auto group = CreateCGroup<TMemory>("something_different");

    group.ForceEmpty();
    int iterations = 0;
    while (group.GetUsageInBytes() != 0) {
        sleep(1);
        ++iterations;
        if (iterations > 10) {
            ASSERT_TRUE(false);
        }
    }

    i64 num = 1;
    auto exitBarier = ::eventfd(0, 0);
    auto initBarier = ::eventfd(0, 0);

    const auto filename = NFS::CombinePaths(group.GetFullPath(), "memory.usage_in_bytes");
    int fd = ::open(~filename, 0);
    int reallyRead = ::read(fd, buffer, 1024);

    ASSERT_TRUE(reallyRead > 0);
    EXPECT_STREQ(~Stroka(buffer, reallyRead), "0\n");

    auto pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        group.AddCurrentTask();

        auto otherPid = fork();
        if (otherPid < 0) {
            YCHECK(::write(initBarier, &num, sizeof(num)) == sizeof(num));
            _exit(3);
        }

        if (otherPid == 0) {
            num = 1;
            YCHECK(::write(initBarier, &num, sizeof(num)) == sizeof(num));
            YCHECK(::read(exitBarier, &num, sizeof(num)) == sizeof(num));
            _exit(2);
        }
        waitpid(otherPid, NULL, 0);
        _exit(1);
    }

    EXPECT_TRUE(::read(initBarier, &num, sizeof(num)) == sizeof(num));

    reallyRead = ::read(fd, buffer, 1024);
    // reallyRead SHOULD BE equal to 0
    ASSERT_TRUE(reallyRead > 0);

    num = 1;
    EXPECT_TRUE(::write(exitBarier, &num, sizeof(num)) == sizeof(num));

    auto waitedpid = waitpid(pid, NULL, 0);
    EXPECT_TRUE(waitedpid == pid);
}

TEST(CGroup, FreezerEmpty)
{
    auto group = CreateCGroup<TFreezer>("some");

    EXPECT_EQ("THAWED", group.GetState());
}

TEST(CGroup, FreezerFreeze)
{
    auto group = CreateCGroup<TFreezer>("some");

    auto addedEvent = eventfd(0, 0);
    auto exitEvent = eventfd(0, 0);

    auto pid = fork();
    ASSERT_TRUE(pid >= 0);

    if (pid == 0) {
        group.AddCurrentTask();

        i64 value = 1024;
        ASSERT_EQ(sizeof(value), ::write(addedEvent, &value, sizeof(value)));

        ASSERT_EQ(sizeof(value), ::read(exitEvent, &value, sizeof(value)));
        exit(0);
    }

    i64 value;
    ASSERT_EQ(sizeof(value), ::read(addedEvent, &value, sizeof(value)));
    // test here something

    group.Freeze();
    auto state = group.GetState();
    EXPECT_TRUE((state == "FREEZING") || (state == "FROZEN"));

    value = 1;
    ASSERT_EQ(sizeof(value), ::write(exitEvent, &value, sizeof(value)));

    group.UnFreeze();

    auto waitedpid = waitpid(pid, nullptr, 0);

    group.Destroy();
    ASSERT_EQ(pid, waitedpid);
}

TEST(CurrentProcessCGroup, Empty)
{
    std::vector<char> empty;
    auto result = ParseCurrentProcessCGroups(TStringBuf(empty.data(), empty.size()));
    EXPECT_TRUE(result.empty());
}

TEST(CurrentProcessCGroup, Basic)
{
    auto basic = STRINGBUF("4:blkio:/\n3:cpuacct:/\n2:freezer:/some\n1:memory:/\n");
    auto result = ParseCurrentProcessCGroups(TStringBuf(basic.data(), basic.length()));
    EXPECT_EQ("", result["blkio"]);
    EXPECT_EQ("", result["cpuacct"]);
    EXPECT_EQ("some", result["freezer"]);
    EXPECT_EQ("", result["memory"]);
    EXPECT_EQ(4, result.size());
}

TEST(CurrentProcessCGroup, Multiple)
{
    auto basic = STRINGBUF("5:cpuacct,cpu,cpuset:/daemons\n");
    auto result = ParseCurrentProcessCGroups(TStringBuf(basic.data(), basic.length()));
    EXPECT_EQ("daemons", result["cpu"]);
    EXPECT_EQ("daemons", result["cpuset"]);
    EXPECT_EQ("daemons", result["cpuacct"]);
    EXPECT_EQ(3, result.size());
}

TEST(CurrentProcessCGroup, BadInput)
{
    auto basic = STRINGBUF("xxx:cpuacct,cpu,cpuset:/daemons\n");
    EXPECT_THROW(ParseCurrentProcessCGroups(TStringBuf(basic.data(), basic.length())), std::exception);
}

class TTestableEvent
    : public NCGroup::TEvent
{
public:
    TTestableEvent(int eventFd, int fd = -1)
        : NCGroup::TEvent(eventFd, fd)
    { }
};

TEST(TEvent, Fired)
{
    auto eventFd = eventfd(0, EFD_NONBLOCK);
    TTestableEvent event(eventFd, -1);

    EXPECT_FALSE(event.Fired());

    i64 value = 1;
    EXPECT_EQ(::write(eventFd, &value, sizeof(value)), sizeof(value));

    EXPECT_TRUE(event.Fired());
}

TEST(TEvent, Sticky)
{
    auto eventFd = eventfd(0, EFD_NONBLOCK);
    TTestableEvent event(eventFd, -1);

    i64 value = 1;
    EXPECT_EQ(::write(eventFd, &value, sizeof(value)), sizeof(value));

    EXPECT_TRUE(event.Fired());
    EXPECT_TRUE(event.Fired());
}

TEST(TEvent, Clear)
{
    auto eventFd = eventfd(0, EFD_NONBLOCK);
    TTestableEvent event(eventFd, -1);

    i64 value = 1;
    EXPECT_EQ(::write(eventFd, &value, sizeof(value)),sizeof(value));

    EXPECT_TRUE(event.Fired());
    event.Clear();
    EXPECT_FALSE(event.Fired());
}

#endif // _linux_

////////////////////////////////////////////////////////////////////////////////

}
} // namespace NCGroup
} // namespace NYT
