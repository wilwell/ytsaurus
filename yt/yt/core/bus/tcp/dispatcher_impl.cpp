#include "dispatcher_impl.h"
#include "config.h"
#include "connection.h"

#include <yt/yt/core/concurrency/periodic_executor.h>
#include <yt/yt/core/concurrency/thread_pool_poller.h>

#include <yt/yt/core/actions/invoker_util.h>

#include <yt/yt/core/ytree/ypath_service.h>
#include <yt/yt/core/ytree/fluent.h>

#include <yt/yt/library/profiling/producer.h>

namespace NYT::NBus {

using namespace NConcurrency;
using namespace NProfiling;
using namespace NNet;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = BusLogger;

static constexpr auto PeriodcCheckPeriod = TDuration::MilliSeconds(100);
static constexpr auto PerConnectionPeriodicCheckPeriod = TDuration::Seconds(10);

////////////////////////////////////////////////////////////////////////////////

TNetworkAddress GetLocalBusAddress(int port)
{
    auto name = Format("yt-local-bus-%v", port);
    return TNetworkAddress::CreateAbstractUnixDomainSocketAddress(name);
}

bool IsLocalBusTransportEnabled()
{
#ifdef _linux_
    return true;
#else
    return false;
#endif
}

////////////////////////////////////////////////////////////////////////////////

TBusNetworkStatistics TBusNetworkCounters::ToStatistics() const
{
    TBusNetworkStatistics result;
#define XX(field) result.field = field.load();
    FOR_EACH_BUS_NETWORK_STATISTICS_FIELD(XX)
#undef XX
    return result;
}

////////////////////////////////////////////////////////////////////////////////

const TIntrusivePtr<TTcpDispatcher::TImpl>& TTcpDispatcher::TImpl::Get()
{
    return TTcpDispatcher::Get()->Impl_;
}

const TBusNetworkCountersPtr& TTcpDispatcher::TImpl::GetCounters(const TString& networkName)
{
    auto [statistics, ok] = NetworkStatistics_.FindOrInsert(networkName, [] {
        return TNetworkStatistics{};
    });

    return statistics->Counters;
}

IPollerPtr TTcpDispatcher::TImpl::GetOrCreatePoller(
    IThreadPoolPollerPtr* pollerPtr,
    bool isXfer,
    const TString& threadNamePrefix)
{
    {
        auto guard = ReaderGuard(PollerLock_);
        if (*pollerPtr) {
            return *pollerPtr;
        }
    }

    IPollerPtr poller;
    {
        auto guard = WriterGuard(PollerLock_);
        if (!*pollerPtr) {
            *pollerPtr = CreateThreadPoolPoller(isXfer ? Config_->ThreadPoolSize : 1, threadNamePrefix);
        }
        poller = *pollerPtr;
    }

    StartPeriodicExecutors();

    return poller;
}

void TTcpDispatcher::TImpl::DisableNetworking()
{
    YT_LOG_INFO("Networking disabled");

    NetworkingDisabled_.store(true);
}

bool TTcpDispatcher::TImpl::IsNetworkingDisabled()
{
    return NetworkingDisabled_.load();
}

const TString& TTcpDispatcher::TImpl::GetNetworkNameForAddress(const TNetworkAddress& address)
{
    if (address.IsUnix()) {
        return LocalNetworkName;
    }

    if (!address.IsIP6()) {
        return DefaultNetworkName;
    }

    auto ip6Address = address.ToIP6Address();

    {
        auto guard = ReaderGuard(NetworksLock_);
        for (const auto& [networkAddress, networkName] : Networks_) {
            if (networkAddress.Contains(ip6Address)) {
                return networkName;
            }
        }
    }

    return DefaultNetworkName;
}

TTosLevel TTcpDispatcher::TImpl::GetTosLevelForBand(EMultiplexingBand band)
{
    if (band < TEnumTraits<EMultiplexingBand>::GetMinValue() || band > TEnumTraits<EMultiplexingBand>::GetMaxValue()) {
        return DefaultTosLevel;
    }
    const auto& bandDescriptor = BandToDescriptor_[band];
    return bandDescriptor.TosLevel.load(std::memory_order::relaxed);
}

IPollerPtr TTcpDispatcher::TImpl::GetAcceptorPoller()
{
    static const TString ThreadNamePrefix("BusAcpt");
    return GetOrCreatePoller(&AcceptorPoller_, false, ThreadNamePrefix);
}

IPollerPtr TTcpDispatcher::TImpl::GetXferPoller()
{
    static const TString ThreadNamePrefix("BusXfer");
    return GetOrCreatePoller(&XferPoller_, true, ThreadNamePrefix);
}

void TTcpDispatcher::TImpl::Configure(const TTcpDispatcherConfigPtr& config)
{
    {
        auto guard = WriterGuard(PollerLock_);

        Config_ = config;

        if (XferPoller_) {
            XferPoller_->Reconfigure(Config_->ThreadPoolSize);
        }
    }

    {
        auto guard = WriterGuard(NetworksLock_);

        Networks_.clear();

        for (const auto& [networkName, networkAddresses] : config->Networks) {
            for (const auto& prefix : networkAddresses) {
                Networks_.emplace_back(prefix, networkName);
            }
        }

        // Put more specific networks first in match order.
        std::sort(Networks_.begin(), Networks_.end(), [] (const auto& lhs, const auto& rhs) {
            return lhs.first.GetMaskSize() > rhs.first.GetMaskSize();
        });
    }

    for (auto band : TEnumTraits<EMultiplexingBand>::GetDomainValues()) {
        const auto& bandConfig = config->MultiplexingBands[band];
        auto& bandDescriptor = BandToDescriptor_[band];
        bandDescriptor.TosLevel.store(bandConfig ? bandConfig->TosLevel : DefaultTosLevel);
    }
}

void TTcpDispatcher::TImpl::RegisterConnection(TTcpConnectionPtr connection)
{
    ConnectionsToRegister_.Enqueue(std::move(connection));
}

void TTcpDispatcher::TImpl::StartPeriodicExecutors()
{
    auto poller = GetXferPoller();
    auto invoker = poller->GetInvoker();

    auto guard = Guard(PeriodicExecutorsLock_);
    if (!PeriodicCheckExecutor_) {
        PeriodicCheckExecutor_ = New<TPeriodicExecutor>(
            invoker,
            BIND(&TImpl::OnPeriodicCheck, MakeWeak(this)),
            PeriodcCheckPeriod);
        PeriodicCheckExecutor_->Start();
    }
}

void TTcpDispatcher::TImpl::CollectSensors(ISensorWriter* writer)
{
    NetworkStatistics_.IterateReadOnly([&] (const auto& name, const auto& statistics) {
        auto counters = statistics.Counters->ToStatistics();
        TWithTagGuard tagGuard(writer, "network", name);
        writer->AddCounter("/in_bytes", counters.InBytes);
        writer->AddCounter("/in_packets", counters.InPackets);
        writer->AddCounter("/out_bytes", counters.OutBytes);
        writer->AddCounter("/out_packets", counters.OutPackets);
        writer->AddGauge("/pending_out_bytes", counters.PendingOutBytes);
        writer->AddGauge("/pending_out_packets", counters.PendingOutPackets);
        writer->AddGauge("/client_connections", counters.ClientConnections);
        writer->AddGauge("/server_connections", counters.ServerConnections);
        writer->AddCounter("/stalled_reads", counters.StalledReads);
        writer->AddCounter("/stalled_writes", counters.StalledWrites);
        writer->AddCounter("/read_errors", counters.ReadErrors);
        writer->AddCounter("/write_errors", counters.WriteErrors);
        writer->AddCounter("/tcp_retransmits", counters.Retransmits);
        writer->AddCounter("/encoder_errors", counters.EncoderErrors);
        writer->AddCounter("/decoder_errors", counters.DecoderErrors);
    });


    TTcpDispatcherConfigPtr config;
    {
        auto guard = ReaderGuard(PollerLock_);
        config = Config_;
    }

    if (config->NetworkBandwidth) {
        writer->AddGauge("/network_bandwidth_limit", *config->NetworkBandwidth);
    }
}

std::vector<TTcpConnectionPtr> TTcpDispatcher::TImpl::GetConnections()
{
    std::vector<TTcpConnectionPtr> result;
    result.reserve(ConnectionList_.size());
    for (const auto& weakConnection : ConnectionList_) {
        if (auto connection = weakConnection.Lock()) {
            result.push_back(connection);
        }
    }
    return result;
}

void TTcpDispatcher::TImpl::BuildOrchid(IYsonConsumer* consumer)
{
    std::vector<std::pair<TTcpConnectionPtr, TBusNetworkStatistics>> connectionsWithStatistics;
    for (const auto& connection : GetConnections()) {
        connectionsWithStatistics.emplace_back(connection, connection->GetBusStatistics());
    }
    SortBy(connectionsWithStatistics, [] (const auto& connectionWithStatistics) {
        return -connectionWithStatistics.second.PendingOutBytes;
    });

    BuildYsonFluently(consumer)
        .BeginMap()
            .Item("connections").DoMapFor(connectionsWithStatistics, [] (auto fluent, const auto& connectionWithStatistics) {
                const auto& [connection, statistics] = connectionWithStatistics;
                fluent
                    .Item(ToString(connection->GetId())).BeginMap()
                        .Item("address").Value(connection->GetEndpointAddress())
                        .Item("statistics").BeginMap()
                            .Item("in_bytes").Value(statistics.InBytes)
                            .Item("in_packets").Value(statistics.InPackets)
                            .Item("out_bytes").Value(statistics.OutBytes)
                            .Item("out_packets").Value(statistics.OutPackets)
                            .Item("pending_out_bytes").Value(statistics.PendingOutBytes)
                            .Item("pending_out_packets").Value(statistics.PendingOutPackets)
                        .EndMap()
                    .EndMap();
            })
        .EndMap();
}


IYPathServicePtr TTcpDispatcher::TImpl::GetOrchidService()
{
    return IYPathService::FromProducer(BIND(&TImpl::BuildOrchid, MakeStrong(this)))
        ->Via(GetXferPoller()->GetInvoker());
}

void TTcpDispatcher::TImpl::OnPeriodicCheck()
{
    for (auto&& connection : ConnectionsToRegister_.DequeueAll()) {
        ConnectionList_.push_back(std::move(connection));
    }

    i64 connectionsToCheck = std::max(
        std::ssize(ConnectionList_) *
        static_cast<i64>(PeriodcCheckPeriod.GetValue()) /
        static_cast<i64>(PerConnectionPeriodicCheckPeriod.GetValue()),
        static_cast<i64>(1));
    for (i64 index = 0; index < connectionsToCheck && !ConnectionList_.empty(); ++index) {
        auto& weakConnection = ConnectionList_[CurrentConnectionListIndex_];
        if (auto connection = weakConnection.Lock()) {
            connection->RunPeriodicCheck();
            ++CurrentConnectionListIndex_;
        } else {
            std::swap(weakConnection, ConnectionList_.back());
            ConnectionList_.pop_back();
        }
        if (CurrentConnectionListIndex_ >= std::ssize(ConnectionList_)) {
            CurrentConnectionListIndex_ = 0;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NBus
