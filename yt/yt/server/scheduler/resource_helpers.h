#pragma once

#include "resource_vector.h"
#include "resource_volume.h"

#include <yt/yt/library/profiling/producer.h>

#include <yt/yt/core/misc/string_builder.h>

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

// For each memory capacity gives the number of nodes with this much memory.
using TMemoryDistribution = THashMap<i64, int>;

TJobResources GetAdjustedResourceLimits(
    const TJobResources& demand,
    const TJobResources& limits,
    const TMemoryDistribution& execNodeMemoryDistribution);

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, const TResourceVector& resourceVector, TStringBuf format);

void ProfileResourceVector(
    NProfiling::ISensorWriter* writer,
    const THashSet<EJobResourceType>& resourceTypes,
    const TResourceVector& resourceVector,
    const TString& prefix);

////////////////////////////////////////////////////////////////////////////////

void ProfileResourceVolume(
    NProfiling::ISensorWriter* writer,
    const TResourceVolume& volume,
    const TString& prefix);

void Serialize(const TResourceVolume& volume, NYson::IYsonConsumer* consumer);
void Deserialize(TResourceVolume& volume, NYTree::INodePtr node);

void FormatValue(TStringBuilderBase* builder, const TResourceVolume& volume, TStringBuf /* format */);
TString ToString(const TResourceVolume& volume);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler


