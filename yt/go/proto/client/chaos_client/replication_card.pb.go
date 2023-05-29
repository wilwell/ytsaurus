// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.30.0
// 	protoc        v3.19.0
// source: yt/yt_proto/yt/client/chaos_client/proto/replication_card.proto

package chaos_client

import (
	misc "go.ytsaurus.tech/yt/go/proto/core/misc"
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type TReplicationProgress struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Segments []*TReplicationProgress_TSegment `protobuf:"bytes,1,rep,name=segments" json:"segments,omitempty"`
	UpperKey []byte                           `protobuf:"bytes,2,req,name=upper_key,json=upperKey" json:"upper_key,omitempty"`
}

func (x *TReplicationProgress) Reset() {
	*x = TReplicationProgress{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReplicationProgress) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReplicationProgress) ProtoMessage() {}

func (x *TReplicationProgress) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReplicationProgress.ProtoReflect.Descriptor instead.
func (*TReplicationProgress) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescGZIP(), []int{0}
}

func (x *TReplicationProgress) GetSegments() []*TReplicationProgress_TSegment {
	if x != nil {
		return x.Segments
	}
	return nil
}

func (x *TReplicationProgress) GetUpperKey() []byte {
	if x != nil {
		return x.UpperKey
	}
	return nil
}

type TReplicaHistoryItem struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Era       *uint64 `protobuf:"varint,1,req,name=era" json:"era,omitempty"`
	Timestamp *uint64 `protobuf:"varint,2,req,name=timestamp" json:"timestamp,omitempty"`
	Mode      *int32  `protobuf:"varint,3,req,name=mode" json:"mode,omitempty"`
	State     *int32  `protobuf:"varint,4,req,name=state" json:"state,omitempty"`
}

func (x *TReplicaHistoryItem) Reset() {
	*x = TReplicaHistoryItem{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReplicaHistoryItem) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReplicaHistoryItem) ProtoMessage() {}

func (x *TReplicaHistoryItem) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReplicaHistoryItem.ProtoReflect.Descriptor instead.
func (*TReplicaHistoryItem) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescGZIP(), []int{1}
}

func (x *TReplicaHistoryItem) GetEra() uint64 {
	if x != nil && x.Era != nil {
		return *x.Era
	}
	return 0
}

func (x *TReplicaHistoryItem) GetTimestamp() uint64 {
	if x != nil && x.Timestamp != nil {
		return *x.Timestamp
	}
	return 0
}

func (x *TReplicaHistoryItem) GetMode() int32 {
	if x != nil && x.Mode != nil {
		return *x.Mode
	}
	return 0
}

func (x *TReplicaHistoryItem) GetState() int32 {
	if x != nil && x.State != nil {
		return *x.State
	}
	return 0
}

type TReplicaInfo struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	ClusterName                  *string                `protobuf:"bytes,1,req,name=cluster_name,json=clusterName" json:"cluster_name,omitempty"`
	ReplicaPath                  *string                `protobuf:"bytes,2,req,name=replica_path,json=replicaPath" json:"replica_path,omitempty"`
	ContentType                  *int32                 `protobuf:"varint,3,req,name=content_type,json=contentType" json:"content_type,omitempty"` // NTabletClient::ETableReplicaContentType
	Mode                         *int32                 `protobuf:"varint,4,req,name=mode" json:"mode,omitempty"`                                  // NTabletClient::ETableReplicaMode
	State                        *int32                 `protobuf:"varint,5,req,name=state" json:"state,omitempty"`                                // NTabletClient::ETableReplicaState
	Progress                     *TReplicationProgress  `protobuf:"bytes,6,opt,name=progress" json:"progress,omitempty"`
	History                      []*TReplicaHistoryItem `protobuf:"bytes,7,rep,name=history" json:"history,omitempty"`
	EnableReplicatedTableTracker *bool                  `protobuf:"varint,8,opt,name=enable_replicated_table_tracker,json=enableReplicatedTableTracker" json:"enable_replicated_table_tracker,omitempty"`
}

func (x *TReplicaInfo) Reset() {
	*x = TReplicaInfo{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReplicaInfo) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReplicaInfo) ProtoMessage() {}

func (x *TReplicaInfo) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReplicaInfo.ProtoReflect.Descriptor instead.
func (*TReplicaInfo) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescGZIP(), []int{2}
}

func (x *TReplicaInfo) GetClusterName() string {
	if x != nil && x.ClusterName != nil {
		return *x.ClusterName
	}
	return ""
}

func (x *TReplicaInfo) GetReplicaPath() string {
	if x != nil && x.ReplicaPath != nil {
		return *x.ReplicaPath
	}
	return ""
}

func (x *TReplicaInfo) GetContentType() int32 {
	if x != nil && x.ContentType != nil {
		return *x.ContentType
	}
	return 0
}

func (x *TReplicaInfo) GetMode() int32 {
	if x != nil && x.Mode != nil {
		return *x.Mode
	}
	return 0
}

func (x *TReplicaInfo) GetState() int32 {
	if x != nil && x.State != nil {
		return *x.State
	}
	return 0
}

func (x *TReplicaInfo) GetProgress() *TReplicationProgress {
	if x != nil {
		return x.Progress
	}
	return nil
}

func (x *TReplicaInfo) GetHistory() []*TReplicaHistoryItem {
	if x != nil {
		return x.History
	}
	return nil
}

func (x *TReplicaInfo) GetEnableReplicatedTableTracker() bool {
	if x != nil && x.EnableReplicatedTableTracker != nil {
		return *x.EnableReplicatedTableTracker
	}
	return false
}

type TReplicationCard struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Replicas                     []*TReplicationCard_TReplicaEntry `protobuf:"bytes,1,rep,name=replicas" json:"replicas,omitempty"`
	CoordinatorCellIds           []*misc.TGuid                     `protobuf:"bytes,2,rep,name=coordinator_cell_ids,json=coordinatorCellIds" json:"coordinator_cell_ids,omitempty"`
	Era                          *uint64                           `protobuf:"varint,3,req,name=era" json:"era,omitempty"`
	TableId                      *misc.TGuid                       `protobuf:"bytes,4,opt,name=table_id,json=tableId" json:"table_id,omitempty"`
	TablePath                    *string                           `protobuf:"bytes,5,opt,name=table_path,json=tablePath" json:"table_path,omitempty"`
	TableClusterName             *string                           `protobuf:"bytes,6,opt,name=table_cluster_name,json=tableClusterName" json:"table_cluster_name,omitempty"`
	CurrentTimestamp             *uint64                           `protobuf:"varint,7,opt,name=current_timestamp,json=currentTimestamp" json:"current_timestamp,omitempty"`
	ReplicatedTableOptions       []byte                            `protobuf:"bytes,8,opt,name=replicated_table_options,json=replicatedTableOptions" json:"replicated_table_options,omitempty"` // NTabletClient::TReplicatedTableOptions
	ReplicationCardCollocationId *misc.TGuid                       `protobuf:"bytes,9,opt,name=replication_card_collocation_id,json=replicationCardCollocationId" json:"replication_card_collocation_id,omitempty"`
}

func (x *TReplicationCard) Reset() {
	*x = TReplicationCard{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[3]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReplicationCard) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReplicationCard) ProtoMessage() {}

func (x *TReplicationCard) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[3]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReplicationCard.ProtoReflect.Descriptor instead.
func (*TReplicationCard) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescGZIP(), []int{3}
}

func (x *TReplicationCard) GetReplicas() []*TReplicationCard_TReplicaEntry {
	if x != nil {
		return x.Replicas
	}
	return nil
}

func (x *TReplicationCard) GetCoordinatorCellIds() []*misc.TGuid {
	if x != nil {
		return x.CoordinatorCellIds
	}
	return nil
}

func (x *TReplicationCard) GetEra() uint64 {
	if x != nil && x.Era != nil {
		return *x.Era
	}
	return 0
}

func (x *TReplicationCard) GetTableId() *misc.TGuid {
	if x != nil {
		return x.TableId
	}
	return nil
}

func (x *TReplicationCard) GetTablePath() string {
	if x != nil && x.TablePath != nil {
		return *x.TablePath
	}
	return ""
}

func (x *TReplicationCard) GetTableClusterName() string {
	if x != nil && x.TableClusterName != nil {
		return *x.TableClusterName
	}
	return ""
}

func (x *TReplicationCard) GetCurrentTimestamp() uint64 {
	if x != nil && x.CurrentTimestamp != nil {
		return *x.CurrentTimestamp
	}
	return 0
}

func (x *TReplicationCard) GetReplicatedTableOptions() []byte {
	if x != nil {
		return x.ReplicatedTableOptions
	}
	return nil
}

func (x *TReplicationCard) GetReplicationCardCollocationId() *misc.TGuid {
	if x != nil {
		return x.ReplicationCardCollocationId
	}
	return nil
}

type TUpstreamReplicationCard struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	ChaosCellId       *misc.TGuid `protobuf:"bytes,1,req,name=chaos_cell_id,json=chaosCellId" json:"chaos_cell_id,omitempty"`                   // NObjectClient::TCellId
	ReplicationCardId *misc.TGuid `protobuf:"bytes,2,req,name=replication_card_id,json=replicationCardId" json:"replication_card_id,omitempty"` // NChaosClient::TReplicationCardId
}

func (x *TUpstreamReplicationCard) Reset() {
	*x = TUpstreamReplicationCard{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[4]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TUpstreamReplicationCard) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TUpstreamReplicationCard) ProtoMessage() {}

func (x *TUpstreamReplicationCard) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[4]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TUpstreamReplicationCard.ProtoReflect.Descriptor instead.
func (*TUpstreamReplicationCard) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescGZIP(), []int{4}
}

func (x *TUpstreamReplicationCard) GetChaosCellId() *misc.TGuid {
	if x != nil {
		return x.ChaosCellId
	}
	return nil
}

func (x *TUpstreamReplicationCard) GetReplicationCardId() *misc.TGuid {
	if x != nil {
		return x.ReplicationCardId
	}
	return nil
}

type TReplicationCardFetchOptions struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	IncludeCoordinators           *bool `protobuf:"varint,1,opt,name=include_coordinators,json=includeCoordinators" json:"include_coordinators,omitempty"`
	IncludeProgress               *bool `protobuf:"varint,2,opt,name=include_progress,json=includeProgress" json:"include_progress,omitempty"`
	IncludeHistory                *bool `protobuf:"varint,3,opt,name=include_history,json=includeHistory" json:"include_history,omitempty"`
	IncludeReplicatedTableOptions *bool `protobuf:"varint,4,opt,name=include_replicated_table_options,json=includeReplicatedTableOptions" json:"include_replicated_table_options,omitempty"`
}

func (x *TReplicationCardFetchOptions) Reset() {
	*x = TReplicationCardFetchOptions{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[5]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReplicationCardFetchOptions) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReplicationCardFetchOptions) ProtoMessage() {}

func (x *TReplicationCardFetchOptions) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[5]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReplicationCardFetchOptions.ProtoReflect.Descriptor instead.
func (*TReplicationCardFetchOptions) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescGZIP(), []int{5}
}

func (x *TReplicationCardFetchOptions) GetIncludeCoordinators() bool {
	if x != nil && x.IncludeCoordinators != nil {
		return *x.IncludeCoordinators
	}
	return false
}

func (x *TReplicationCardFetchOptions) GetIncludeProgress() bool {
	if x != nil && x.IncludeProgress != nil {
		return *x.IncludeProgress
	}
	return false
}

func (x *TReplicationCardFetchOptions) GetIncludeHistory() bool {
	if x != nil && x.IncludeHistory != nil {
		return *x.IncludeHistory
	}
	return false
}

func (x *TReplicationCardFetchOptions) GetIncludeReplicatedTableOptions() bool {
	if x != nil && x.IncludeReplicatedTableOptions != nil {
		return *x.IncludeReplicatedTableOptions
	}
	return false
}

type TReplicationProgress_TSegment struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	LowerKey  []byte  `protobuf:"bytes,1,req,name=lower_key,json=lowerKey" json:"lower_key,omitempty"`
	Timestamp *uint64 `protobuf:"varint,2,req,name=timestamp" json:"timestamp,omitempty"`
}

func (x *TReplicationProgress_TSegment) Reset() {
	*x = TReplicationProgress_TSegment{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[6]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReplicationProgress_TSegment) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReplicationProgress_TSegment) ProtoMessage() {}

func (x *TReplicationProgress_TSegment) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[6]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReplicationProgress_TSegment.ProtoReflect.Descriptor instead.
func (*TReplicationProgress_TSegment) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescGZIP(), []int{0, 0}
}

func (x *TReplicationProgress_TSegment) GetLowerKey() []byte {
	if x != nil {
		return x.LowerKey
	}
	return nil
}

func (x *TReplicationProgress_TSegment) GetTimestamp() uint64 {
	if x != nil && x.Timestamp != nil {
		return *x.Timestamp
	}
	return 0
}

type TReplicationCard_TReplicaEntry struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Id   *misc.TGuid   `protobuf:"bytes,1,req,name=id" json:"id,omitempty"`
	Info *TReplicaInfo `protobuf:"bytes,2,req,name=info" json:"info,omitempty"`
}

func (x *TReplicationCard_TReplicaEntry) Reset() {
	*x = TReplicationCard_TReplicaEntry{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[7]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TReplicationCard_TReplicaEntry) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TReplicationCard_TReplicaEntry) ProtoMessage() {}

func (x *TReplicationCard_TReplicaEntry) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[7]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TReplicationCard_TReplicaEntry.ProtoReflect.Descriptor instead.
func (*TReplicationCard_TReplicaEntry) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescGZIP(), []int{3, 0}
}

func (x *TReplicationCard_TReplicaEntry) GetId() *misc.TGuid {
	if x != nil {
		return x.Id
	}
	return nil
}

func (x *TReplicationCard_TReplicaEntry) GetInfo() *TReplicaInfo {
	if x != nil {
		return x.Info
	}
	return nil
}

var File_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto protoreflect.FileDescriptor

var file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDesc = []byte{
	0x0a, 0x3f, 0x79, 0x74, 0x2f, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74,
	0x2f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2f, 0x63, 0x68, 0x61, 0x6f, 0x73, 0x5f, 0x63, 0x6c,
	0x69, 0x65, 0x6e, 0x74, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x72, 0x65, 0x70, 0x6c, 0x69,
	0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x5f, 0x63, 0x61, 0x72, 0x64, 0x2e, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x12, 0x17, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x43, 0x68, 0x61, 0x6f, 0x73, 0x43, 0x6c, 0x69,
	0x65, 0x6e, 0x74, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x26, 0x79, 0x74, 0x5f, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x6d, 0x69, 0x73,
	0x63, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x67, 0x75, 0x69, 0x64, 0x2e, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x22, 0xce, 0x01, 0x0a, 0x14, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74,
	0x69, 0x6f, 0x6e, 0x50, 0x72, 0x6f, 0x67, 0x72, 0x65, 0x73, 0x73, 0x12, 0x52, 0x0a, 0x08, 0x73,
	0x65, 0x67, 0x6d, 0x65, 0x6e, 0x74, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x36, 0x2e,
	0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x43, 0x68, 0x61, 0x6f, 0x73, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74,
	0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61,
	0x74, 0x69, 0x6f, 0x6e, 0x50, 0x72, 0x6f, 0x67, 0x72, 0x65, 0x73, 0x73, 0x2e, 0x54, 0x53, 0x65,
	0x67, 0x6d, 0x65, 0x6e, 0x74, 0x52, 0x08, 0x73, 0x65, 0x67, 0x6d, 0x65, 0x6e, 0x74, 0x73, 0x12,
	0x1b, 0x0a, 0x09, 0x75, 0x70, 0x70, 0x65, 0x72, 0x5f, 0x6b, 0x65, 0x79, 0x18, 0x02, 0x20, 0x02,
	0x28, 0x0c, 0x52, 0x08, 0x75, 0x70, 0x70, 0x65, 0x72, 0x4b, 0x65, 0x79, 0x1a, 0x45, 0x0a, 0x08,
	0x54, 0x53, 0x65, 0x67, 0x6d, 0x65, 0x6e, 0x74, 0x12, 0x1b, 0x0a, 0x09, 0x6c, 0x6f, 0x77, 0x65,
	0x72, 0x5f, 0x6b, 0x65, 0x79, 0x18, 0x01, 0x20, 0x02, 0x28, 0x0c, 0x52, 0x08, 0x6c, 0x6f, 0x77,
	0x65, 0x72, 0x4b, 0x65, 0x79, 0x12, 0x1c, 0x0a, 0x09, 0x74, 0x69, 0x6d, 0x65, 0x73, 0x74, 0x61,
	0x6d, 0x70, 0x18, 0x02, 0x20, 0x02, 0x28, 0x04, 0x52, 0x09, 0x74, 0x69, 0x6d, 0x65, 0x73, 0x74,
	0x61, 0x6d, 0x70, 0x22, 0x6f, 0x0a, 0x13, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x48,
	0x69, 0x73, 0x74, 0x6f, 0x72, 0x79, 0x49, 0x74, 0x65, 0x6d, 0x12, 0x10, 0x0a, 0x03, 0x65, 0x72,
	0x61, 0x18, 0x01, 0x20, 0x02, 0x28, 0x04, 0x52, 0x03, 0x65, 0x72, 0x61, 0x12, 0x1c, 0x0a, 0x09,
	0x74, 0x69, 0x6d, 0x65, 0x73, 0x74, 0x61, 0x6d, 0x70, 0x18, 0x02, 0x20, 0x02, 0x28, 0x04, 0x52,
	0x09, 0x74, 0x69, 0x6d, 0x65, 0x73, 0x74, 0x61, 0x6d, 0x70, 0x12, 0x12, 0x0a, 0x04, 0x6d, 0x6f,
	0x64, 0x65, 0x18, 0x03, 0x20, 0x02, 0x28, 0x05, 0x52, 0x04, 0x6d, 0x6f, 0x64, 0x65, 0x12, 0x14,
	0x0a, 0x05, 0x73, 0x74, 0x61, 0x74, 0x65, 0x18, 0x04, 0x20, 0x02, 0x28, 0x05, 0x52, 0x05, 0x73,
	0x74, 0x61, 0x74, 0x65, 0x22, 0xfb, 0x02, 0x0a, 0x0c, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69, 0x63,
	0x61, 0x49, 0x6e, 0x66, 0x6f, 0x12, 0x21, 0x0a, 0x0c, 0x63, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72,
	0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x01, 0x20, 0x02, 0x28, 0x09, 0x52, 0x0b, 0x63, 0x6c, 0x75,
	0x73, 0x74, 0x65, 0x72, 0x4e, 0x61, 0x6d, 0x65, 0x12, 0x21, 0x0a, 0x0c, 0x72, 0x65, 0x70, 0x6c,
	0x69, 0x63, 0x61, 0x5f, 0x70, 0x61, 0x74, 0x68, 0x18, 0x02, 0x20, 0x02, 0x28, 0x09, 0x52, 0x0b,
	0x72, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x50, 0x61, 0x74, 0x68, 0x12, 0x21, 0x0a, 0x0c, 0x63,
	0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x5f, 0x74, 0x79, 0x70, 0x65, 0x18, 0x03, 0x20, 0x02, 0x28,
	0x05, 0x52, 0x0b, 0x63, 0x6f, 0x6e, 0x74, 0x65, 0x6e, 0x74, 0x54, 0x79, 0x70, 0x65, 0x12, 0x12,
	0x0a, 0x04, 0x6d, 0x6f, 0x64, 0x65, 0x18, 0x04, 0x20, 0x02, 0x28, 0x05, 0x52, 0x04, 0x6d, 0x6f,
	0x64, 0x65, 0x12, 0x14, 0x0a, 0x05, 0x73, 0x74, 0x61, 0x74, 0x65, 0x18, 0x05, 0x20, 0x02, 0x28,
	0x05, 0x52, 0x05, 0x73, 0x74, 0x61, 0x74, 0x65, 0x12, 0x49, 0x0a, 0x08, 0x70, 0x72, 0x6f, 0x67,
	0x72, 0x65, 0x73, 0x73, 0x18, 0x06, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x2d, 0x2e, 0x4e, 0x59, 0x54,
	0x2e, 0x4e, 0x43, 0x68, 0x61, 0x6f, 0x73, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2e, 0x4e, 0x50,
	0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f,
	0x6e, 0x50, 0x72, 0x6f, 0x67, 0x72, 0x65, 0x73, 0x73, 0x52, 0x08, 0x70, 0x72, 0x6f, 0x67, 0x72,
	0x65, 0x73, 0x73, 0x12, 0x46, 0x0a, 0x07, 0x68, 0x69, 0x73, 0x74, 0x6f, 0x72, 0x79, 0x18, 0x07,
	0x20, 0x03, 0x28, 0x0b, 0x32, 0x2c, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x43, 0x68, 0x61, 0x6f,
	0x73, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54,
	0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x48, 0x69, 0x73, 0x74, 0x6f, 0x72, 0x79, 0x49, 0x74,
	0x65, 0x6d, 0x52, 0x07, 0x68, 0x69, 0x73, 0x74, 0x6f, 0x72, 0x79, 0x12, 0x45, 0x0a, 0x1f, 0x65,
	0x6e, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x72, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x65, 0x64,
	0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x74, 0x72, 0x61, 0x63, 0x6b, 0x65, 0x72, 0x18, 0x08,
	0x20, 0x01, 0x28, 0x08, 0x52, 0x1c, 0x65, 0x6e, 0x61, 0x62, 0x6c, 0x65, 0x52, 0x65, 0x70, 0x6c,
	0x69, 0x63, 0x61, 0x74, 0x65, 0x64, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x54, 0x72, 0x61, 0x63, 0x6b,
	0x65, 0x72, 0x22, 0xe9, 0x04, 0x0a, 0x10, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74,
	0x69, 0x6f, 0x6e, 0x43, 0x61, 0x72, 0x64, 0x12, 0x53, 0x0a, 0x08, 0x72, 0x65, 0x70, 0x6c, 0x69,
	0x63, 0x61, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x37, 0x2e, 0x4e, 0x59, 0x54, 0x2e,
	0x4e, 0x43, 0x68, 0x61, 0x6f, 0x73, 0x43, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2e, 0x4e, 0x50, 0x72,
	0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e,
	0x43, 0x61, 0x72, 0x64, 0x2e, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x45, 0x6e, 0x74,
	0x72, 0x79, 0x52, 0x08, 0x72, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x73, 0x12, 0x43, 0x0a, 0x14,
	0x63, 0x6f, 0x6f, 0x72, 0x64, 0x69, 0x6e, 0x61, 0x74, 0x6f, 0x72, 0x5f, 0x63, 0x65, 0x6c, 0x6c,
	0x5f, 0x69, 0x64, 0x73, 0x18, 0x02, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x11, 0x2e, 0x4e, 0x59, 0x54,
	0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x47, 0x75, 0x69, 0x64, 0x52, 0x12, 0x63,
	0x6f, 0x6f, 0x72, 0x64, 0x69, 0x6e, 0x61, 0x74, 0x6f, 0x72, 0x43, 0x65, 0x6c, 0x6c, 0x49, 0x64,
	0x73, 0x12, 0x10, 0x0a, 0x03, 0x65, 0x72, 0x61, 0x18, 0x03, 0x20, 0x02, 0x28, 0x04, 0x52, 0x03,
	0x65, 0x72, 0x61, 0x12, 0x2c, 0x0a, 0x08, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x69, 0x64, 0x18,
	0x04, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x11, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f,
	0x74, 0x6f, 0x2e, 0x54, 0x47, 0x75, 0x69, 0x64, 0x52, 0x07, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x49,
	0x64, 0x12, 0x1d, 0x0a, 0x0a, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x70, 0x61, 0x74, 0x68, 0x18,
	0x05, 0x20, 0x01, 0x28, 0x09, 0x52, 0x09, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x50, 0x61, 0x74, 0x68,
	0x12, 0x2c, 0x0a, 0x12, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x63, 0x6c, 0x75, 0x73, 0x74, 0x65,
	0x72, 0x5f, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x06, 0x20, 0x01, 0x28, 0x09, 0x52, 0x10, 0x74, 0x61,
	0x62, 0x6c, 0x65, 0x43, 0x6c, 0x75, 0x73, 0x74, 0x65, 0x72, 0x4e, 0x61, 0x6d, 0x65, 0x12, 0x2b,
	0x0a, 0x11, 0x63, 0x75, 0x72, 0x72, 0x65, 0x6e, 0x74, 0x5f, 0x74, 0x69, 0x6d, 0x65, 0x73, 0x74,
	0x61, 0x6d, 0x70, 0x18, 0x07, 0x20, 0x01, 0x28, 0x04, 0x52, 0x10, 0x63, 0x75, 0x72, 0x72, 0x65,
	0x6e, 0x74, 0x54, 0x69, 0x6d, 0x65, 0x73, 0x74, 0x61, 0x6d, 0x70, 0x12, 0x38, 0x0a, 0x18, 0x72,
	0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x65, 0x64, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x5f,
	0x6f, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x18, 0x08, 0x20, 0x01, 0x28, 0x0c, 0x52, 0x16, 0x72,
	0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x65, 0x64, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x4f, 0x70,
	0x74, 0x69, 0x6f, 0x6e, 0x73, 0x12, 0x58, 0x0a, 0x1f, 0x72, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61,
	0x74, 0x69, 0x6f, 0x6e, 0x5f, 0x63, 0x61, 0x72, 0x64, 0x5f, 0x63, 0x6f, 0x6c, 0x6c, 0x6f, 0x63,
	0x61, 0x74, 0x69, 0x6f, 0x6e, 0x5f, 0x69, 0x64, 0x18, 0x09, 0x20, 0x01, 0x28, 0x0b, 0x32, 0x11,
	0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x47, 0x75, 0x69,
	0x64, 0x52, 0x1c, 0x72, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x43, 0x61,
	0x72, 0x64, 0x43, 0x6f, 0x6c, 0x6c, 0x6f, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x49, 0x64, 0x1a,
	0x6d, 0x0a, 0x0d, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x45, 0x6e, 0x74, 0x72, 0x79,
	0x12, 0x21, 0x0a, 0x02, 0x69, 0x64, 0x18, 0x01, 0x20, 0x02, 0x28, 0x0b, 0x32, 0x11, 0x2e, 0x4e,
	0x59, 0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x47, 0x75, 0x69, 0x64, 0x52,
	0x02, 0x69, 0x64, 0x12, 0x39, 0x0a, 0x04, 0x69, 0x6e, 0x66, 0x6f, 0x18, 0x02, 0x20, 0x02, 0x28,
	0x0b, 0x32, 0x25, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x43, 0x68, 0x61, 0x6f, 0x73, 0x43, 0x6c,
	0x69, 0x65, 0x6e, 0x74, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x52, 0x65, 0x70,
	0x6c, 0x69, 0x63, 0x61, 0x49, 0x6e, 0x66, 0x6f, 0x52, 0x04, 0x69, 0x6e, 0x66, 0x6f, 0x22, 0x94,
	0x01, 0x0a, 0x18, 0x54, 0x55, 0x70, 0x73, 0x74, 0x72, 0x65, 0x61, 0x6d, 0x52, 0x65, 0x70, 0x6c,
	0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x43, 0x61, 0x72, 0x64, 0x12, 0x35, 0x0a, 0x0d, 0x63,
	0x68, 0x61, 0x6f, 0x73, 0x5f, 0x63, 0x65, 0x6c, 0x6c, 0x5f, 0x69, 0x64, 0x18, 0x01, 0x20, 0x02,
	0x28, 0x0b, 0x32, 0x11, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e,
	0x54, 0x47, 0x75, 0x69, 0x64, 0x52, 0x0b, 0x63, 0x68, 0x61, 0x6f, 0x73, 0x43, 0x65, 0x6c, 0x6c,
	0x49, 0x64, 0x12, 0x41, 0x0a, 0x13, 0x72, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f,
	0x6e, 0x5f, 0x63, 0x61, 0x72, 0x64, 0x5f, 0x69, 0x64, 0x18, 0x02, 0x20, 0x02, 0x28, 0x0b, 0x32,
	0x11, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x47, 0x75,
	0x69, 0x64, 0x52, 0x11, 0x72, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x43,
	0x61, 0x72, 0x64, 0x49, 0x64, 0x22, 0xee, 0x01, 0x0a, 0x1c, 0x54, 0x52, 0x65, 0x70, 0x6c, 0x69,
	0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x43, 0x61, 0x72, 0x64, 0x46, 0x65, 0x74, 0x63, 0x68, 0x4f,
	0x70, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x12, 0x31, 0x0a, 0x14, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64,
	0x65, 0x5f, 0x63, 0x6f, 0x6f, 0x72, 0x64, 0x69, 0x6e, 0x61, 0x74, 0x6f, 0x72, 0x73, 0x18, 0x01,
	0x20, 0x01, 0x28, 0x08, 0x52, 0x13, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x43, 0x6f, 0x6f,
	0x72, 0x64, 0x69, 0x6e, 0x61, 0x74, 0x6f, 0x72, 0x73, 0x12, 0x29, 0x0a, 0x10, 0x69, 0x6e, 0x63,
	0x6c, 0x75, 0x64, 0x65, 0x5f, 0x70, 0x72, 0x6f, 0x67, 0x72, 0x65, 0x73, 0x73, 0x18, 0x02, 0x20,
	0x01, 0x28, 0x08, 0x52, 0x0f, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x50, 0x72, 0x6f, 0x67,
	0x72, 0x65, 0x73, 0x73, 0x12, 0x27, 0x0a, 0x0f, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x5f,
	0x68, 0x69, 0x73, 0x74, 0x6f, 0x72, 0x79, 0x18, 0x03, 0x20, 0x01, 0x28, 0x08, 0x52, 0x0e, 0x69,
	0x6e, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x48, 0x69, 0x73, 0x74, 0x6f, 0x72, 0x79, 0x12, 0x47, 0x0a,
	0x20, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x5f, 0x72, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61,
	0x74, 0x65, 0x64, 0x5f, 0x74, 0x61, 0x62, 0x6c, 0x65, 0x5f, 0x6f, 0x70, 0x74, 0x69, 0x6f, 0x6e,
	0x73, 0x18, 0x04, 0x20, 0x01, 0x28, 0x08, 0x52, 0x1d, 0x69, 0x6e, 0x63, 0x6c, 0x75, 0x64, 0x65,
	0x52, 0x65, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x65, 0x64, 0x54, 0x61, 0x62, 0x6c, 0x65, 0x4f,
	0x70, 0x74, 0x69, 0x6f, 0x6e, 0x73, 0x42, 0x32, 0x5a, 0x30, 0x61, 0x2e, 0x79, 0x61, 0x6e, 0x64,
	0x65, 0x78, 0x2d, 0x74, 0x65, 0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f, 0x79, 0x74, 0x2f, 0x67, 0x6f,
	0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2f, 0x63, 0x68,
	0x61, 0x6f, 0x73, 0x5f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74,
}

var (
	file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescOnce sync.Once
	file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescData = file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDesc
)

func file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescGZIP() []byte {
	file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescOnce.Do(func() {
		file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescData = protoimpl.X.CompressGZIP(file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescData)
	})
	return file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDescData
}

var file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes = make([]protoimpl.MessageInfo, 8)
var file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_goTypes = []interface{}{
	(*TReplicationProgress)(nil),           // 0: NYT.NChaosClient.NProto.TReplicationProgress
	(*TReplicaHistoryItem)(nil),            // 1: NYT.NChaosClient.NProto.TReplicaHistoryItem
	(*TReplicaInfo)(nil),                   // 2: NYT.NChaosClient.NProto.TReplicaInfo
	(*TReplicationCard)(nil),               // 3: NYT.NChaosClient.NProto.TReplicationCard
	(*TUpstreamReplicationCard)(nil),       // 4: NYT.NChaosClient.NProto.TUpstreamReplicationCard
	(*TReplicationCardFetchOptions)(nil),   // 5: NYT.NChaosClient.NProto.TReplicationCardFetchOptions
	(*TReplicationProgress_TSegment)(nil),  // 6: NYT.NChaosClient.NProto.TReplicationProgress.TSegment
	(*TReplicationCard_TReplicaEntry)(nil), // 7: NYT.NChaosClient.NProto.TReplicationCard.TReplicaEntry
	(*misc.TGuid)(nil),                     // 8: NYT.NProto.TGuid
}
var file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_depIdxs = []int32{
	6,  // 0: NYT.NChaosClient.NProto.TReplicationProgress.segments:type_name -> NYT.NChaosClient.NProto.TReplicationProgress.TSegment
	0,  // 1: NYT.NChaosClient.NProto.TReplicaInfo.progress:type_name -> NYT.NChaosClient.NProto.TReplicationProgress
	1,  // 2: NYT.NChaosClient.NProto.TReplicaInfo.history:type_name -> NYT.NChaosClient.NProto.TReplicaHistoryItem
	7,  // 3: NYT.NChaosClient.NProto.TReplicationCard.replicas:type_name -> NYT.NChaosClient.NProto.TReplicationCard.TReplicaEntry
	8,  // 4: NYT.NChaosClient.NProto.TReplicationCard.coordinator_cell_ids:type_name -> NYT.NProto.TGuid
	8,  // 5: NYT.NChaosClient.NProto.TReplicationCard.table_id:type_name -> NYT.NProto.TGuid
	8,  // 6: NYT.NChaosClient.NProto.TReplicationCard.replication_card_collocation_id:type_name -> NYT.NProto.TGuid
	8,  // 7: NYT.NChaosClient.NProto.TUpstreamReplicationCard.chaos_cell_id:type_name -> NYT.NProto.TGuid
	8,  // 8: NYT.NChaosClient.NProto.TUpstreamReplicationCard.replication_card_id:type_name -> NYT.NProto.TGuid
	8,  // 9: NYT.NChaosClient.NProto.TReplicationCard.TReplicaEntry.id:type_name -> NYT.NProto.TGuid
	2,  // 10: NYT.NChaosClient.NProto.TReplicationCard.TReplicaEntry.info:type_name -> NYT.NChaosClient.NProto.TReplicaInfo
	11, // [11:11] is the sub-list for method output_type
	11, // [11:11] is the sub-list for method input_type
	11, // [11:11] is the sub-list for extension type_name
	11, // [11:11] is the sub-list for extension extendee
	0,  // [0:11] is the sub-list for field type_name
}

func init() { file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_init() }
func file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_init() {
	if File_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReplicationProgress); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReplicaHistoryItem); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReplicaInfo); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[3].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReplicationCard); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[4].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TUpstreamReplicationCard); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[5].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReplicationCardFetchOptions); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[6].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReplicationProgress_TSegment); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes[7].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TReplicationCard_TReplicaEntry); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   8,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_goTypes,
		DependencyIndexes: file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_depIdxs,
		MessageInfos:      file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_msgTypes,
	}.Build()
	File_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto = out.File
	file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_rawDesc = nil
	file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_goTypes = nil
	file_yt_yt_proto_yt_client_chaos_client_proto_replication_card_proto_depIdxs = nil
}
