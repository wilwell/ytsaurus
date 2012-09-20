#include "stdafx.h"
#include "fair_share_strategy.h"
#include "scheduler_strategy.h"
#include "master_connector.h"
#include "job_resources.h"

#include <ytlib/ytree/yson_serializable.h>
#include <ytlib/ytree/ypath_proxy.h>
#include <ytlib/ytree/fluent.h>

#include <ytlib/object_client/object_service_proxy.h>

#include <ytlib/logging/log.h>

namespace NYT {
namespace NScheduler {

using namespace NYTree;
using namespace NObjectClient;

////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = SchedulerLogger;
static NProfiling::TProfiler& Profiler = SchedulerProfiler;
static Stroka DefaultPoolId("default");
static const double RatioPrecision = 1e-12;

////////////////////////////////////////////////////////////////////

struct ISchedulerElement;
typedef TIntrusivePtr<ISchedulerElement> ISchedulerElementPtr;

struct ISchedulableElement;
typedef TIntrusivePtr<ISchedulableElement> ISchedulableElementPtr;

struct IElementRanking;

class TOperationElement;
typedef TIntrusivePtr<TOperationElement> TOperationElementPtr;

class TCompositeSchedulerElement;
typedef TIntrusivePtr<TCompositeSchedulerElement> TCompositeSchedulerElementPtr;

class TPool;
typedef TIntrusivePtr<TPool> TPoolPtr;

class TRootElement;
typedef TIntrusivePtr<TRootElement> TRootElementPtr;

////////////////////////////////////////////////////////////////////

struct ISchedulerElement
    : public virtual TRefCounted
{
    virtual void Update(double limitsRatio) = 0;
    virtual void ScheduleJobs(ISchedulingContext* context) = 0;
};

////////////////////////////////////////////////////////////////////

struct ISchedulableElement
    : public virtual ISchedulerElement
{
    virtual TInstant GetStartTime() const = 0;
    
    virtual double GetWeight() const = 0;
    virtual double GetMinShareRatio() const = 0;
    
    virtual NProto::TNodeResources GetDemand() const = 0;
    virtual NProto::TNodeResources GetUtilization() const = 0;
};

////////////////////////////////////////////////////////////////////

DECLARE_ENUM(EOperationStatus,
    (Normal)
    (StarvingForMinShare)
    (StarvingForFairShare)
);

class TOperationElement
    : public ISchedulableElement
{
public:
    explicit TOperationElement(ISchedulerStrategyHost* host, TOperationPtr operation)
        : Host(host)
        , Operation(operation)
        , Pool(NULL)
        , EffectiveLimits(ZeroResources())
    { }


    virtual void ScheduleJobs(ISchedulingContext* context) override
    {
        while (context->HasSpareResources()) {
            if (Operation->GetState() != EOperationState::Running) {
                break;
            }
            auto job = Operation->GetController()->ScheduleJob(context);
            if (!job) {
                break;
            }
        }
    }

    virtual void Update(double limitsRatio) override
    {
        UNUSED(limitsRatio);

        ComputeEffectiveLimits();
    }


    virtual TInstant GetStartTime() const override
    {
        return Operation->GetStartTime();
    }

    virtual double GetWeight() const override
    {
        return Spec->Weight;
    }

    virtual double GetMinShareRatio() const override
    {
        return Spec->MinShareRatio;
    }

    virtual NProto::TNodeResources GetDemand() const override
    {
        auto controller = Operation->GetController();
        auto result = controller->GetUsedResources();
        AddResources(&result, controller->GetNeededResources());
        return result;
    }

    virtual NProto::TNodeResources GetUtilization() const override
    {
        auto controller = Operation->GetController();
        return controller->GetUsedResources();
    }


    const NProto::TNodeResources& GetEffectiveLimits() const
    {
        return EffectiveLimits;
    }


    TPooledOperationSpecPtr GetSpec() const
    {
        return Spec;
    }

    void SetSpec(TPooledOperationSpecPtr newSpec)
    {
        Spec = newSpec;
    }


    TPoolPtr GetPool() const
    {
        return Pool;
    }

    void SetPool(TPoolPtr pool)
    {
        Pool = ~pool;
    }


private:
    ISchedulerStrategyHost* Host;
    TOperationPtr Operation;

    TPool* Pool;
    TPooledOperationSpecPtr Spec;

    NProto::TNodeResources EffectiveLimits;


    void ComputeEffectiveLimits()
    {
        if (Operation->GetState() != EOperationState::Running) {
            EffectiveLimits = Host->GetTotalResourceLimits();
            return;
        }

        EffectiveLimits = ZeroResources();
        auto quantum = Operation->GetController()->GetMinNeededResources();

        // Sort jobs by node.
        std::vector<TJobPtr> jobs(Operation->Jobs().begin(), Operation->Jobs().end());
        std::sort(
            jobs.begin(),
            jobs.end(),
            [] (const TJobPtr& lhs, const TJobPtr& rhs) {
                return lhs->GetNode() < rhs->GetNode();
            });

        // Sort nodes.
        auto nodes = Host->GetExecNodes();
        sort(nodes.begin(), nodes.end());

        // Merge jobs and nodes.
        auto jobIt = jobs.begin();
        auto nodeIt = nodes.begin();
        while (jobIt != jobs.end() || nodeIt != nodes.end()) {
            auto jobGroupBeginIt = jobIt;
            auto jobGroupEndIt = jobIt;
            while (jobGroupEndIt != jobs.end() && (*jobGroupEndIt)->GetNode() == (*jobGroupBeginIt)->GetNode()) {
                ++jobGroupEndIt;
            }

            while (nodeIt != nodes.end() && (jobGroupBeginIt == jobs.end() || *nodeIt < (*jobGroupBeginIt)->GetNode())) {
                // Node without jobs.
                const auto& node = *nodeIt;
                AddResources(
                    &EffectiveLimits,
                    NScheduler::ComputeEffectiveLimits(node->ResourceLimits(), quantum));
                ++nodeIt;
            }

            if (nodeIt != nodes.end()) {
                // Node with jobs.
                const auto& node = *nodeIt;
                YASSERT(node == (*jobGroupBeginIt)->GetNode());
                auto nodeLimits = node->ResourceLimits();
                for (auto jobGroupIt = jobGroupBeginIt; jobGroupIt != jobGroupEndIt; ++jobGroupIt) {
                    const auto& job = *jobGroupIt;
                    SubtractResources(&nodeLimits, job->ResourceUtilization());
                    AddResources(&EffectiveLimits, job->ResourceUtilization());
                }
                AddResources(
                    &EffectiveLimits,
                    NScheduler::ComputeEffectiveLimits(nodeLimits, quantum));
                ++nodeIt;
            }

            jobIt = jobGroupEndIt;
        }
    }

};

////////////////////////////////////////////////////////////////////

struct TFairShareAttributes
{
    explicit TFairShareAttributes(ISchedulableElementPtr element)
        : Element(element)
        , Rank(0)
        , DominantResource(EResourceType::Cpu)
        , Weight(0.0)
        , DemandRatio(0.0)
        , FairShareRatio(0.0)
        , AdjustedMinShareRatio(0.0)
    { }

    ISchedulableElementPtr Element;
    int Rank;
    EResourceType DominantResource;
    double Weight;
    double DemandRatio;
    double FairShareRatio;
    double AdjustedMinShareRatio;
};

////////////////////////////////////////////////////////////////////

class TCompositeSchedulerElement
    : public virtual ISchedulerElement
{
public:
    explicit TCompositeSchedulerElement(ISchedulerStrategyHost* host)
        : Host(host)
        , Mode(ESchedulingMode::Fifo)
        , LimitsRatio(0.0)
        , Limits(ZeroResources())
    { }

    virtual void Update(double limitsRatio) override
    {
        LimitsRatio = limitsRatio;
        Limits = Host->GetTotalResourceLimits();
        MultiplyResources(&Limits, limitsRatio);

        ComputeFairShares();
    }

    virtual void ScheduleJobs(ISchedulingContext* context) override
    {
        auto sortedChildren = GetSortedChildren();
        FOREACH (auto* attributes, sortedChildren) {
            if (!context->HasSpareResources()) {
                break;
            }
            attributes->Element->ScheduleJobs(context);
        }
    }


    void AddChild(ISchedulableElementPtr child)
    {
        TFairShareAttributes attributes(child);
        YCHECK(Children.insert(std::make_pair(child, attributes)).second);
    }

    void RemoveChild(ISchedulableElementPtr child)
    {
        YCHECK(Children.erase(child) == 1);
        // Avoid scheduling removed children.
        ComputeFairShares();
    }

    const TFairShareAttributes& GetChildAttributes(ISchedulableElementPtr child) const
    {
        auto it = Children.find(child);
        YCHECK(it != Children.end());
        return it->second;
    }

    std::vector<ISchedulableElementPtr> GetChildren() const
    {
        std::vector<ISchedulableElementPtr> result;
        FOREACH (const auto& pair, Children) {
            result.push_back(pair.first);
        }
        return result;
    }

protected:
    ISchedulerStrategyHost* Host;

    ESchedulingMode Mode;
    
    yhash_map<ISchedulableElementPtr, TFairShareAttributes> Children;
    
    double LimitsRatio;
    NProto::TNodeResources Limits;


    void ComputeFairShares()
    {
        // Choose dominant resource types.
        // Precache weights.
        // Precache min share ratios and compute their sum.
        // Compute demand ratios and their sum.
        double demandRatioSum = 0.0;
        double minShareRatioSum = 0.0;
        auto totalLimits = Host->GetTotalResourceLimits();
        FOREACH (auto& pair, Children) {
            auto& attributes = pair.second;

            auto demand = attributes.Element->GetDemand();
            attributes.DominantResource = GetDominantResource(demand, totalLimits);
            i64 dominantLimits = GetResource(totalLimits, attributes.DominantResource);
            i64 dominantDemand = GetResource(demand, attributes.DominantResource);
            attributes.DemandRatio = dominantLimits == 0 ? LimitsRatio : (double) dominantDemand / dominantLimits;
            demandRatioSum += attributes.DemandRatio;

            attributes.Weight = attributes.Element->GetWeight();

            attributes.AdjustedMinShareRatio = attributes.Element->GetMinShareRatio() * LimitsRatio;
            minShareRatioSum += attributes.AdjustedMinShareRatio;

        }

        // Scale down weights if needed.
        if (minShareRatioSum > LimitsRatio) {
            FOREACH (auto& pair, Children) {
                auto& attributes = pair.second;
                attributes.AdjustedMinShareRatio *= (LimitsRatio / minShareRatioSum);
            }
        }

        // Check for FIFO mode.
        // Check if we have more resources than totally demanded by children.
        if (Mode == ESchedulingMode::Fifo) {
            // Set fair shares equal to limits ratio. This is done just for convenience.
            SetFifoFairShares();
        } else if (demandRatioSum <= LimitsRatio) {
            // Easy case -- just give everyone what he needs.
            SetDemandedFairShares();
        } else {
            // Hard case -- compute fair shares using fit factor.
            ComputeFairSharesByFitting();
        }

        // Propagate updates to children.
        FOREACH (auto& pair, Children) {
            auto& attributes = pair.second;           
            attributes.Element->Update(attributes.FairShareRatio);
        }
    }

    void SetFifoFairShares()
    {
        FOREACH (auto& pair, Children) {
            auto& attributes = pair.second;
            attributes.FairShareRatio = LimitsRatio;
        }
    }

    void SetDemandedFairShares()
    {
        FOREACH (auto& pair, Children) {
            auto& attributes = pair.second;
            attributes.FairShareRatio = attributes.DemandRatio;
        }
    }

    void ComputeFairSharesByFitting()
    {
        auto computeFairShareRatio = [&] (double fitFactor, const TFairShareAttributes& attributes) -> double {
            double result = attributes.Weight * fitFactor;
            // Never give less than promised by min share.
            result = std::max(result, attributes.AdjustedMinShareRatio);
            // Never give more than demanded.
            result = std::min(result, attributes.DemandRatio);
            return result;
        };

        // Run binary search to compute fit factor.
        double fitFactorLo = 0.0;
        double fitFactorHi = 1.0;
        while (fitFactorHi - fitFactorLo > RatioPrecision) {
            double fitFactor = (fitFactorLo + fitFactorHi) / 2.0;
            double fairShareRatioSum = 0.0;
            FOREACH (const auto& pair, Children) {
                fairShareRatioSum += computeFairShareRatio(fitFactor, pair.second);
            }
            if (fairShareRatioSum < LimitsRatio) {
                fitFactorLo = fitFactor;
            } else {
                fitFactorHi = fitFactor;
            }
        }

        // Compute fair share ratios.
        double fitFactor = (fitFactorLo + fitFactorHi) / 2.0;
        FOREACH (auto& pair, Children) {
            auto& attributes = pair.second;
            attributes.FairShareRatio = computeFairShareRatio(fitFactor, attributes);
        }
    }


    const std::vector<TFairShareAttributes*> GetSortedChildren()
    {
        PROFILE_TIMING ("/fair_share_sort_time") {
            std::vector<TFairShareAttributes*> sortedChildren;
            FOREACH (auto& pair, Children) {
                sortedChildren.push_back(&pair.second);
            }

            switch (Mode) {
                case ESchedulingMode::Fifo:
                    SortChildrenFifo(&sortedChildren);
                    break;
                case ESchedulingMode::FairShare:
                    SortChildrenFairShare(&sortedChildren);
                    break;
                default:
                    YUNREACHABLE();
            }

            // Update ranks.
            for (int rank = 0; rank < static_cast<int>(sortedChildren.size()); ++rank) {
                sortedChildren[rank]->Rank = rank;
            }

            return sortedChildren;
        }
    }

    void SortChildrenFifo(std::vector<TFairShareAttributes*>* sortedChildren)
    {
        // Sort by weight (desc), then by start time (asc).
        std::sort(
            sortedChildren->begin(),
            sortedChildren->end(),
            [] (const TFairShareAttributes* lhs, const TFairShareAttributes* rhs) -> bool {
                const auto& lhsElement = lhs->Element;
                const auto& rhsElement = rhs->Element;
                if (lhsElement->GetWeight() > rhsElement->GetWeight()) {
                    return true;
                }
                if (lhsElement->GetWeight() < rhsElement->GetWeight()) {
                    return false;
                }
                return lhsElement->GetStartTime() < rhsElement->GetStartTime();
            });
    }

    void SortChildrenFairShare(std::vector<TFairShareAttributes*>* sortedChildren)
    {
        std::sort(
            sortedChildren->begin(),
            sortedChildren->end(),
            [&] (const TFairShareAttributes* lhs, const TFairShareAttributes* rhs) -> bool {
                bool lhsNeedy = IsNeedy(*lhs);
                bool rhsNeedy = IsNeedy(*rhs);

                if (lhsNeedy && !rhsNeedy) {
                    return true;
                }

                if (!lhsNeedy && rhsNeedy) {
                    return false;
                }

                if (lhsNeedy && rhsNeedy) {
                    double lhsUseToTotalRatio = GetUtilizationToLimitsRatio(*lhs);
                    double rhsUseToTotalRatio = GetUtilizationToLimitsRatio(*rhs);
                    return lhsUseToTotalRatio < rhsUseToTotalRatio;
                }

                {
                    double lhsUseToWeightRatio = GetUtilizationToLimitsRatio(*lhs);
                    double rhsUseToWeightRatio = GetUtilizationToLimitsRatio(*rhs);
                    return lhsUseToWeightRatio < rhsUseToWeightRatio;
                }
            });
    }


    bool IsNeedy(const TFairShareAttributes& attributes)
    {
        i64 demand = GetResource(attributes.Element->GetDemand(), attributes.DominantResource);
        i64 utilization = GetResource(attributes.Element->GetUtilization(), attributes.DominantResource);
        i64 limits = GetResource(Limits, attributes.DominantResource);
        return utilization < demand && utilization < limits * attributes.AdjustedMinShareRatio;
    }

    double GetUtilizationToLimitsRatio(const TFairShareAttributes& attributes)
    {
        i64 use = GetResource(attributes.Element->GetUtilization(), attributes.DominantResource);
        i64 total = std::max(GetResource(Limits, attributes.DominantResource), static_cast<i64>(1));
        return (double) use / total;
    }

    static double GetUtilizationToWeightRatio(const TFairShareAttributes& attributes)
    {
        i64 utilization = GetResource(attributes.Element->GetUtilization(), attributes.DominantResource);
        double weight = std::max(attributes.Weight, 1.0);
        return (double) utilization / weight;
    }


    void SetMode(ESchedulingMode mode)
    {
        if (Mode != mode) {
            Mode = mode;
            ComputeFairShares();
        }
    }

};

////////////////////////////////////////////////////////////////////

class TPool
    : public TCompositeSchedulerElement
    , public virtual ISchedulableElement
{
public:
    TPool(
        ISchedulerStrategyHost* host,
        const Stroka& id)
        : TCompositeSchedulerElement(host)
        , Id(id)
    {
        SetDefaultConfig();
    }


    const Stroka& GetId() const
    {
        return Id;
    }


    TPoolConfigPtr GetConfig()
    {
        return Config;
    }

    void SetConfig(TPoolConfigPtr newConfig)
    {
        Config = newConfig;
        SetMode(Config->Mode);
    }

    void SetDefaultConfig()
    {
        SetConfig(New<TPoolConfig>());
    }


    virtual TInstant GetStartTime() const override
    {
        // Makes no sense for pools since the root is in fair-share mode.
        return TInstant();
    }

    virtual double GetWeight() const override
    {
        return Config->Weight;
    }

    virtual double GetMinShareRatio() const override
    {
        return Config->MinShareRatio;
    }

    virtual NProto::TNodeResources GetDemand() const override
    {
        auto result = ZeroResources();
        FOREACH (const auto& pair, Children) {
            AddResources(&result, pair.second.Element->GetDemand());
        }
        return result;
    }

    virtual NProto::TNodeResources GetUtilization() const override
    {
        auto result = ZeroResources();
        FOREACH (const auto& pair, Children) {
            AddResources(&result, pair.second.Element->GetUtilization());
        }
        return result;
    }


private:
    Stroka Id;

    TPoolConfigPtr Config;

};

////////////////////////////////////////////////////////////////////

class TRootElement
    : public TCompositeSchedulerElement
{
public:
    explicit TRootElement(ISchedulerStrategyHost* host)
        : TCompositeSchedulerElement(host)
    {
        SetMode(ESchedulingMode::FairShare);
    }
};

////////////////////////////////////////////////////////////////////

class TFairShareStrategy
    : public ISchedulerStrategy
{
public:
    explicit TFairShareStrategy(
        TFairShareStrategyConfigPtr config,
        ISchedulerStrategyHost* host)
        : Config(config)
        , Host(host)
    {
        Host->SubscribeOperationStarted(BIND(&TFairShareStrategy::OnOperationStarted, this));
        Host->SubscribeOperationFinished(BIND(&TFairShareStrategy::OnOperationFinished, this));

        auto* masterConnector = Host->GetMasterConnector();
        masterConnector->SubscribeWatcherRequest(BIND(&TFairShareStrategy::OnPoolsRequest, this));
        masterConnector->SubscribeWatcherResponse(BIND(&TFairShareStrategy::OnPoolsResponse, this));

        RootElement = New<TRootElement>(Host);

        DefaultPool = New<TPool>(Host, DefaultPoolId);
        RegisterPool(DefaultPool);
    }

    virtual void ScheduleJobs(ISchedulingContext* context) override
    {
        auto now = TInstant::Now();
        if (!LastUpdateTime || now > LastUpdateTime.Get() + Config->FairShareUpdatePeriod) {
            PROFILE_TIMING ("/fair_share_update_time") {
                RootElement->Update(1.0);
            }
            LastUpdateTime = now;
        }
        return RootElement->ScheduleJobs(context);
    }

    virtual void BuildOperationProgressYson(TOperationPtr operation, IYsonConsumer* consumer) override
    {
        auto element = GetOperationElement(operation);
        auto pool = element->GetPool();
        const auto& attributes = pool->GetChildAttributes(element);
        BuildYsonMapFluently(consumer)
            .Item("pool").Scalar(pool->GetId())
            .Item("start_time").Scalar(element->GetStartTime())
            .Item("effective_resource_limits").Do(BIND(&BuildNodeResourcesYson, element->GetEffectiveLimits()))
            .Item("fair_share_status").Scalar(GetOperationStatus(element))
            .Do(BIND(&TFairShareStrategy::BuildElementYson, pool, element));
    }

    virtual void BuildOrchidYson(NYTree::IYsonConsumer* consumer) override
    {
        BuildYsonMapFluently(consumer)
            .Item("pools").DoMapFor(Pools, [&] (TFluentMap fluent, const TPoolMap::value_type& pair) {
                const auto& id = pair.first;
                auto pool = pair.second;
                auto config = pool->GetConfig();
                fluent
                    .Item(id).BeginMap()
                        .Item("mode").Scalar(config->Mode)
                        .Do(BIND(&TFairShareStrategy::BuildElementYson, RootElement, pool))
                    .EndMap();
            });
    }

private:
    TFairShareStrategyConfigPtr Config;
    ISchedulerStrategyHost* Host;

    typedef yhash_map<Stroka, TPoolPtr> TPoolMap;
    TPoolMap Pools;
    TPoolPtr DefaultPool;

    yhash_map<TOperationPtr, TOperationElementPtr> OperationToElement;

    TRootElementPtr RootElement;
    TNullable<TInstant> LastUpdateTime;

    void OnOperationStarted(TOperationPtr operation)
    {
        auto operationElement = New<TOperationElement>(Host, operation);
        YCHECK(OperationToElement.insert(std::make_pair(operation, operationElement)).second);

        TPooledOperationSpecPtr spec;
        try {
            spec = ConvertTo<TPooledOperationSpecPtr>(operation->GetSpec());
        } catch (const std::exception& ex) {
            LOG_ERROR(ex, "Error parsing spec of pooled operation %s, defaults will be used",
                ~ToString(operation->GetOperationId()));
            spec = New<TPooledOperationSpec>();
        }

        TPoolPtr pool;
        if (spec->Pool) {
            pool = FindPool(spec->Pool.Get());
            if (!pool) {
                LOG_ERROR("Invalid pool %s specified for operation %s, using %s",
                    ~spec->Pool.Get().Quote(),
                    ~ToString(operation->GetOperationId()),
                    ~DefaultPool->GetId().Quote());
            }
        }
        if (!pool) {
            pool = DefaultPool;
        }

        operationElement->SetSpec(spec);

        operationElement->SetPool(~pool);
        pool->AddChild(operationElement);

        LOG_INFO("Operation added to pool (OperationId: %s, PoolId: %s)",
            ~ToString(operation->GetOperationId()),
            ~pool->GetId());
    }

    void OnOperationFinished(TOperationPtr operation)
    {
        auto element = GetOperationElement(operation);
        auto pool = element->GetPool();

        YCHECK(OperationToElement.erase(operation) == 1);
        pool->RemoveChild(element);

        LOG_INFO("Operation removed from pool (OperationId: %s, PoolId: %s)",
            ~ToString(operation->GetOperationId()),
            ~pool->GetId());
    }


    void RegisterPool(TPoolPtr pool)
    {
        YCHECK(Pools.insert(std::make_pair(pool->GetId(), pool)).second);
        RootElement->AddChild(pool);

        LOG_INFO("Pool registered: %s", ~pool->GetId());
    }

    void UnregisterPool(TPoolPtr pool)
    {
        YCHECK(pool != DefaultPool);

        // Move all operations to the default pool.
        auto children = pool->GetChildren();
        FOREACH (auto child, children) {
            auto* operationElement = dynamic_cast<TOperationElement*>(~child);
            operationElement->SetPool(DefaultPool);
            DefaultPool->AddChild(operationElement);
        }

        // Remove the pool.
        YCHECK(Pools.erase(pool->GetId()) == 1);
        RootElement->RemoveChild(pool);

        LOG_INFO("Pool unregistered: %s", ~pool->GetId());
    }

    TPoolPtr FindPool(const Stroka& id)
    {
        auto it = Pools.find(id);
        return it == Pools.end() ? NULL : it->second;
    }

    TPoolPtr GetPool(const Stroka& id)
    {
        auto pool = FindPool(id);
        YCHECK(pool);
        return pool;
    }


    TOperationElementPtr FindOperationElement(TOperationPtr operation)
    {
        auto it = OperationToElement.find(operation);
        return it == OperationToElement.end() ? NULL : it->second;
    }

    TOperationElementPtr GetOperationElement(TOperationPtr operation)
    {
        auto element = FindOperationElement(operation);
        YCHECK(element);
        return element;
    }


    void OnPoolsRequest(TObjectServiceProxy::TReqExecuteBatchPtr batchReq)
    {
        LOG_INFO("Updating pools");

        auto req = TYPathProxy::Get("//sys/scheduler/pools");
        auto poolConfigKeys = DefaultPool->GetConfig()->GetRegisteredKeys();
        ToProto(req->mutable_attributes(), poolConfigKeys);
        batchReq->AddRequest(req, "get_pools");
    }

    void OnPoolsResponse(TObjectServiceProxy::TRspExecuteBatchPtr batchRsp)
    {
        auto rsp = batchRsp->GetResponse<TYPathProxy::TRspGet>("get_pools");
        THROW_ERROR_EXCEPTION_IF_FAILED(*rsp);

        // Build the set of potential orphans.
        yhash_set<Stroka> orphanPoolIds;
        FOREACH (const auto& pair, Pools) {
            const auto& id = pair.first;
            const auto& pool = pair.second;
            YCHECK(orphanPoolIds.insert(pair.first).second);
        }

        auto newPoolsNode = ConvertToNode(TYsonString(rsp->value()));
        auto newPoolsMapNode = newPoolsNode->AsMap();
        FOREACH (const auto& pair, newPoolsMapNode->GetChildren()) {
            const auto& id = pair.first;
            const auto& poolNode = pair.second;

            // Parse config.
            auto configNode = ConvertToNode(poolNode->Attributes());
            TPoolConfigPtr config;
            try {
                config = ConvertTo<TPoolConfigPtr>(configNode);
            } catch (const std::exception& ex) {
                LOG_ERROR(ex, "Error parsing configuration of pool %s, defaults will be used",
                    ~id.Quote());
                config = New<TPoolConfig>();
            }

            auto existingPool = FindPool(id);
            if (existingPool) {
                // Reconfigure existing pool.
                existingPool->SetConfig(config);
                YCHECK(orphanPoolIds.erase(id) == 1);
            } else {
                // Create new pool.
                auto newPool = New<TPool>(Host, id);
                newPool->SetConfig(config);
                RegisterPool(newPool);
            }
        }

        // Unregister orphan pools.
        FOREACH (const auto& id, orphanPoolIds) {
            auto pool = GetPool(id);
            if (pool == DefaultPool) {
                // Default pool is always present.
                // When it's configuration vanishes it just gets the default config.
                pool->SetDefaultConfig();
            } else {
                UnregisterPool(pool);
            }
        }

        LOG_INFO("Pools updated");
    }


    EOperationStatus GetOperationStatus(TOperationElementPtr element) const
    {
        auto pool = element->GetPool();
        const auto& attributes = pool->GetChildAttributes(element);

        // For operations in FIFO pools only the top one may starve.
        if (pool->GetConfig()->Mode == ESchedulingMode::Fifo && attributes.Rank != 0) {
            return EOperationStatus::Normal;
        }

        i64 utilization = GetResource(element->GetUtilization(), attributes.DominantResource);
        i64 limits = GetResource(element->GetEffectiveLimits(), attributes.DominantResource);
        double utilizationRatio = limits == 0 ? 1.0 : (double) utilization / limits;

        if (utilizationRatio < attributes.AdjustedMinShareRatio) {
            return EOperationStatus::StarvingForMinShare;
        }

        if (utilizationRatio < attributes.FairShareRatio * Config->FairShareStarvationFactor) {
            return EOperationStatus::StarvingForFairShare;
        }

        return EOperationStatus::Normal;
    }


    static void BuildElementYson(
        TCompositeSchedulerElementPtr composite,
        ISchedulableElementPtr element,
        IYsonConsumer* consumer)
    {
        const auto& attributes = composite->GetChildAttributes(element);
        BuildYsonMapFluently(consumer)
            .Item("scheduling_rank").Scalar(attributes.Rank)
            .Item("resource_demand").Do(BIND(&BuildNodeResourcesYson, element->GetDemand()))
            .Item("resource_utilization").Do(BIND(&BuildNodeResourcesYson, element->GetUtilization()))
            .Item("dominant_resource").Scalar(attributes.DominantResource)
            .Item("weight").Scalar(element->GetWeight())
            .Item("min_share_ratio").Scalar(element->GetMinShareRatio())
            .Item("adjusted_min_share_ratio").Scalar(attributes.AdjustedMinShareRatio)
            .Item("demand_ratio").Scalar(attributes.DemandRatio)
            .Item("fair_share_ratio").Scalar(attributes.FairShareRatio);
    }

};

TAutoPtr<ISchedulerStrategy> CreateFairShareStrategy(
    TFairShareStrategyConfigPtr config,
    ISchedulerStrategyHost* host)
{
    return new TFairShareStrategy(config, host);
}

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

