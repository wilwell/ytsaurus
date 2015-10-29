#include "stdafx.h"
#include "scheduler_channel.h"
#include "config.h"

#include <ytlib/object_client/object_service_proxy.h>

#include <core/ytree/convert.h>
#include <core/ytree/ypath_proxy.h>
#include <core/ytree/fluent.h>

#include <core/bus/config.h>
#include <core/bus/tcp_client.h>

#include <core/rpc/roaming_channel.h>
#include <core/rpc/bus_channel.h>
#include <core/rpc/retrying_channel.h>

namespace NYT {
namespace NScheduler {

using namespace NBus;
using namespace NRpc;
using namespace NObjectClient;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

class TSchedulerChannelProvider
    : public IRoamingChannelProvider
{
public:
    TSchedulerChannelProvider(IChannelFactoryPtr channelFactory, IChannelPtr masterChannel)
        : ChannelFactory_(std::move(channelFactory))
        , MasterChannel_(std::move(masterChannel))
        , EndpointDescription_(Format("Scheduler@%v",
            MasterChannel_->GetEndpointDescription()))
        , EndpointAttributes_(ConvertToAttributes(BuildYsonStringFluently()
            .BeginMap()
                .Item("scheduler").Value(true)
                .Items(MasterChannel_->GetEndpointAttributes())
            .EndMap()))
    { }

    virtual const Stroka& GetEndpointDescription() const override
    {
        return EndpointDescription_;
    }

    virtual const NYTree::IAttributeDictionary& GetEndpointAttributes() const override
    {
        return *EndpointAttributes_;
    }

    virtual TFuture<IChannelPtr> GetChannel(const Stroka& /*serviceName*/) override
    {
        {
            TGuard<TSpinLock> guard(SpinLock_);
            if (CachedChannel_) {
                return MakeFuture(CachedChannel_);
            }
        }

        TObjectServiceProxy proxy(MasterChannel_);
        auto batchReq = proxy.ExecuteBatch();
        batchReq->AddRequest(TYPathProxy::Get("//sys/scheduler/@address"));
        return batchReq->Invoke()
            .Apply(BIND([=, this_ = MakeStrong(this)] (TObjectServiceProxy::TRspExecuteBatchPtr batchRsp) -> IChannelPtr {
                auto rsp = batchRsp->GetResponse<TYPathProxy::TRspGet>(0);
                if (rsp.FindMatching(NYT::NYTree::EErrorCode::ResolveError)) {
                    THROW_ERROR_EXCEPTION("No scheduler is configured");
                }

                THROW_ERROR_EXCEPTION_IF_FAILED(rsp, "Cannot determine scheduler address");

                auto address = ConvertTo<Stroka>(TYsonString(rsp.Value()->value()));

                auto channel = ChannelFactory_->CreateChannel(address);
                channel = CreateFailureDetectingChannel(
                    channel,
                    BIND(&TSchedulerChannelProvider::OnChannelFailed, MakeWeak(this)));

                {
                    TGuard<TSpinLock> guard(SpinLock_);
                    CachedChannel_ = channel;
                }

                return channel;
            }));
    }

    virtual TFuture<void> Terminate(const TError& error) override
    {
        TGuard<TSpinLock> guard(SpinLock_);
        return CachedChannel_ ? CachedChannel_->Terminate(error) : VoidFuture;
    }

private:
    const IChannelFactoryPtr ChannelFactory_;
    const IChannelPtr MasterChannel_;

    const Stroka EndpointDescription_;
    const std::unique_ptr<IAttributeDictionary> EndpointAttributes_;

    TSpinLock SpinLock_;
    IChannelPtr CachedChannel_;


    void OnChannelFailed(IChannelPtr channel)
    {
        TGuard<TSpinLock> guard(SpinLock_);
        if (CachedChannel_ != channel) {
            return;
        }
        CachedChannel_.Reset();
    }

};

IChannelPtr CreateSchedulerChannel(
    TSchedulerConnectionConfigPtr config,
    IChannelFactoryPtr channelFactory,
    IChannelPtr masterChannel)
{
    YCHECK(config);
    YCHECK(channelFactory);
    YCHECK(masterChannel);

    auto channelProvider = New<TSchedulerChannelProvider>(channelFactory, masterChannel);
    auto roamingChannel = CreateRoamingChannel(channelProvider);
    roamingChannel->SetDefaultTimeout(config->RpcTimeout);
    return CreateRetryingChannel(config, roamingChannel);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
