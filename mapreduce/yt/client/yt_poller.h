#pragma once

#include <mapreduce/yt/interface/client.h>

#include <util/generic/list.h>
#include <util/system/mutex.h>
#include <util/system/thread.h>
#include <util/system/condvar.h>

namespace NYT {
namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

class IYtPollerItem
    : public TThrRefBase
{
public:
    enum EStatus {
        PollContinue,
        PollBreak,
    };

public:
    virtual ~IYtPollerItem() = default;

    virtual void PrepareRequest(IBatchRequest* batchRequest) = 0;

    // Should return PollContinue if poller should continue polling this item.
    // Should return PollBreak if poller should stop polling this item.
    virtual EStatus OnRequestExecuted() = 0;
};
using IYtPollerItemPtr = ::TIntrusivePtr<IYtPollerItem>;

////////////////////////////////////////////////////////////////////////////////

class TYtPoller
    : public TThrRefBase
{
public:
    explicit TYtPoller(IClientPtr Client_);
    ~TYtPoller();

    void Watch(IYtPollerItemPtr item);

private:
    void WatchLoop();
    static void* WatchLoopProc(void*);
    void Stop();

private:
    struct TItem;

    IClientPtr const Client_;

    ylist<IYtPollerItemPtr> InProgress_;
    ylist<IYtPollerItemPtr> Pending_;

    TThread WaiterThread_;
    TMutex Lock_;
    TCondVar HasData_;

    bool IsRunning_ = true;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail
} // namespace NYT
