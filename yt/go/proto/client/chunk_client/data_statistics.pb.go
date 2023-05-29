// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.30.0
// 	protoc        v3.19.0
// source: yt/yt_proto/yt/client/chunk_client/proto/data_statistics.proto

package chunk_client

import (
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

type TDataStatistics struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	UncompressedDataSize *int64 `protobuf:"varint,1,opt,name=uncompressed_data_size,json=uncompressedDataSize,def=0" json:"uncompressed_data_size,omitempty"`
	CompressedDataSize   *int64 `protobuf:"varint,2,opt,name=compressed_data_size,json=compressedDataSize,def=0" json:"compressed_data_size,omitempty"`
	RowCount             *int64 `protobuf:"varint,3,opt,name=row_count,json=rowCount,def=0" json:"row_count,omitempty"`
	ChunkCount           *int64 `protobuf:"varint,4,opt,name=chunk_count,json=chunkCount,def=0" json:"chunk_count,omitempty"`
	RegularDiskSpace     *int64 `protobuf:"varint,6,opt,name=regular_disk_space,json=regularDiskSpace,def=0" json:"regular_disk_space,omitempty"`
	ErasureDiskSpace     *int64 `protobuf:"varint,7,opt,name=erasure_disk_space,json=erasureDiskSpace,def=0" json:"erasure_disk_space,omitempty"`
	// For backward compatibility this can be -1 which means "invalid value".
	DataWeight         *int64 `protobuf:"varint,8,opt,name=data_weight,json=dataWeight,def=0" json:"data_weight,omitempty"`
	UnmergedRowCount   *int64 `protobuf:"varint,9,opt,name=unmerged_row_count,json=unmergedRowCount,def=0" json:"unmerged_row_count,omitempty"`
	UnmergedDataWeight *int64 `protobuf:"varint,10,opt,name=unmerged_data_weight,json=unmergedDataWeight,def=0" json:"unmerged_data_weight,omitempty"`
}

// Default values for TDataStatistics fields.
const (
	Default_TDataStatistics_UncompressedDataSize = int64(0)
	Default_TDataStatistics_CompressedDataSize   = int64(0)
	Default_TDataStatistics_RowCount             = int64(0)
	Default_TDataStatistics_ChunkCount           = int64(0)
	Default_TDataStatistics_RegularDiskSpace     = int64(0)
	Default_TDataStatistics_ErasureDiskSpace     = int64(0)
	Default_TDataStatistics_DataWeight           = int64(0)
	Default_TDataStatistics_UnmergedRowCount     = int64(0)
	Default_TDataStatistics_UnmergedDataWeight   = int64(0)
)

func (x *TDataStatistics) Reset() {
	*x = TDataStatistics{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TDataStatistics) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TDataStatistics) ProtoMessage() {}

func (x *TDataStatistics) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TDataStatistics.ProtoReflect.Descriptor instead.
func (*TDataStatistics) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDescGZIP(), []int{0}
}

func (x *TDataStatistics) GetUncompressedDataSize() int64 {
	if x != nil && x.UncompressedDataSize != nil {
		return *x.UncompressedDataSize
	}
	return Default_TDataStatistics_UncompressedDataSize
}

func (x *TDataStatistics) GetCompressedDataSize() int64 {
	if x != nil && x.CompressedDataSize != nil {
		return *x.CompressedDataSize
	}
	return Default_TDataStatistics_CompressedDataSize
}

func (x *TDataStatistics) GetRowCount() int64 {
	if x != nil && x.RowCount != nil {
		return *x.RowCount
	}
	return Default_TDataStatistics_RowCount
}

func (x *TDataStatistics) GetChunkCount() int64 {
	if x != nil && x.ChunkCount != nil {
		return *x.ChunkCount
	}
	return Default_TDataStatistics_ChunkCount
}

func (x *TDataStatistics) GetRegularDiskSpace() int64 {
	if x != nil && x.RegularDiskSpace != nil {
		return *x.RegularDiskSpace
	}
	return Default_TDataStatistics_RegularDiskSpace
}

func (x *TDataStatistics) GetErasureDiskSpace() int64 {
	if x != nil && x.ErasureDiskSpace != nil {
		return *x.ErasureDiskSpace
	}
	return Default_TDataStatistics_ErasureDiskSpace
}

func (x *TDataStatistics) GetDataWeight() int64 {
	if x != nil && x.DataWeight != nil {
		return *x.DataWeight
	}
	return Default_TDataStatistics_DataWeight
}

func (x *TDataStatistics) GetUnmergedRowCount() int64 {
	if x != nil && x.UnmergedRowCount != nil {
		return *x.UnmergedRowCount
	}
	return Default_TDataStatistics_UnmergedRowCount
}

func (x *TDataStatistics) GetUnmergedDataWeight() int64 {
	if x != nil && x.UnmergedDataWeight != nil {
		return *x.UnmergedDataWeight
	}
	return Default_TDataStatistics_UnmergedDataWeight
}

var File_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto protoreflect.FileDescriptor

var file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDesc = []byte{
	0x0a, 0x3e, 0x79, 0x74, 0x2f, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74,
	0x2f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74, 0x2f, 0x63, 0x68, 0x75, 0x6e, 0x6b, 0x5f, 0x63, 0x6c,
	0x69, 0x65, 0x6e, 0x74, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x64, 0x61, 0x74, 0x61, 0x5f,
	0x73, 0x74, 0x61, 0x74, 0x69, 0x73, 0x74, 0x69, 0x63, 0x73, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x12, 0x17, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x43, 0x68, 0x75, 0x6e, 0x6b, 0x43, 0x6c, 0x69, 0x65,
	0x6e, 0x74, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0xaf, 0x03, 0x0a, 0x0f, 0x54, 0x44,
	0x61, 0x74, 0x61, 0x53, 0x74, 0x61, 0x74, 0x69, 0x73, 0x74, 0x69, 0x63, 0x73, 0x12, 0x37, 0x0a,
	0x16, 0x75, 0x6e, 0x63, 0x6f, 0x6d, 0x70, 0x72, 0x65, 0x73, 0x73, 0x65, 0x64, 0x5f, 0x64, 0x61,
	0x74, 0x61, 0x5f, 0x73, 0x69, 0x7a, 0x65, 0x18, 0x01, 0x20, 0x01, 0x28, 0x03, 0x3a, 0x01, 0x30,
	0x52, 0x14, 0x75, 0x6e, 0x63, 0x6f, 0x6d, 0x70, 0x72, 0x65, 0x73, 0x73, 0x65, 0x64, 0x44, 0x61,
	0x74, 0x61, 0x53, 0x69, 0x7a, 0x65, 0x12, 0x33, 0x0a, 0x14, 0x63, 0x6f, 0x6d, 0x70, 0x72, 0x65,
	0x73, 0x73, 0x65, 0x64, 0x5f, 0x64, 0x61, 0x74, 0x61, 0x5f, 0x73, 0x69, 0x7a, 0x65, 0x18, 0x02,
	0x20, 0x01, 0x28, 0x03, 0x3a, 0x01, 0x30, 0x52, 0x12, 0x63, 0x6f, 0x6d, 0x70, 0x72, 0x65, 0x73,
	0x73, 0x65, 0x64, 0x44, 0x61, 0x74, 0x61, 0x53, 0x69, 0x7a, 0x65, 0x12, 0x1e, 0x0a, 0x09, 0x72,
	0x6f, 0x77, 0x5f, 0x63, 0x6f, 0x75, 0x6e, 0x74, 0x18, 0x03, 0x20, 0x01, 0x28, 0x03, 0x3a, 0x01,
	0x30, 0x52, 0x08, 0x72, 0x6f, 0x77, 0x43, 0x6f, 0x75, 0x6e, 0x74, 0x12, 0x22, 0x0a, 0x0b, 0x63,
	0x68, 0x75, 0x6e, 0x6b, 0x5f, 0x63, 0x6f, 0x75, 0x6e, 0x74, 0x18, 0x04, 0x20, 0x01, 0x28, 0x03,
	0x3a, 0x01, 0x30, 0x52, 0x0a, 0x63, 0x68, 0x75, 0x6e, 0x6b, 0x43, 0x6f, 0x75, 0x6e, 0x74, 0x12,
	0x2f, 0x0a, 0x12, 0x72, 0x65, 0x67, 0x75, 0x6c, 0x61, 0x72, 0x5f, 0x64, 0x69, 0x73, 0x6b, 0x5f,
	0x73, 0x70, 0x61, 0x63, 0x65, 0x18, 0x06, 0x20, 0x01, 0x28, 0x03, 0x3a, 0x01, 0x30, 0x52, 0x10,
	0x72, 0x65, 0x67, 0x75, 0x6c, 0x61, 0x72, 0x44, 0x69, 0x73, 0x6b, 0x53, 0x70, 0x61, 0x63, 0x65,
	0x12, 0x2f, 0x0a, 0x12, 0x65, 0x72, 0x61, 0x73, 0x75, 0x72, 0x65, 0x5f, 0x64, 0x69, 0x73, 0x6b,
	0x5f, 0x73, 0x70, 0x61, 0x63, 0x65, 0x18, 0x07, 0x20, 0x01, 0x28, 0x03, 0x3a, 0x01, 0x30, 0x52,
	0x10, 0x65, 0x72, 0x61, 0x73, 0x75, 0x72, 0x65, 0x44, 0x69, 0x73, 0x6b, 0x53, 0x70, 0x61, 0x63,
	0x65, 0x12, 0x22, 0x0a, 0x0b, 0x64, 0x61, 0x74, 0x61, 0x5f, 0x77, 0x65, 0x69, 0x67, 0x68, 0x74,
	0x18, 0x08, 0x20, 0x01, 0x28, 0x03, 0x3a, 0x01, 0x30, 0x52, 0x0a, 0x64, 0x61, 0x74, 0x61, 0x57,
	0x65, 0x69, 0x67, 0x68, 0x74, 0x12, 0x2f, 0x0a, 0x12, 0x75, 0x6e, 0x6d, 0x65, 0x72, 0x67, 0x65,
	0x64, 0x5f, 0x72, 0x6f, 0x77, 0x5f, 0x63, 0x6f, 0x75, 0x6e, 0x74, 0x18, 0x09, 0x20, 0x01, 0x28,
	0x03, 0x3a, 0x01, 0x30, 0x52, 0x10, 0x75, 0x6e, 0x6d, 0x65, 0x72, 0x67, 0x65, 0x64, 0x52, 0x6f,
	0x77, 0x43, 0x6f, 0x75, 0x6e, 0x74, 0x12, 0x33, 0x0a, 0x14, 0x75, 0x6e, 0x6d, 0x65, 0x72, 0x67,
	0x65, 0x64, 0x5f, 0x64, 0x61, 0x74, 0x61, 0x5f, 0x77, 0x65, 0x69, 0x67, 0x68, 0x74, 0x18, 0x0a,
	0x20, 0x01, 0x28, 0x03, 0x3a, 0x01, 0x30, 0x52, 0x12, 0x75, 0x6e, 0x6d, 0x65, 0x72, 0x67, 0x65,
	0x64, 0x44, 0x61, 0x74, 0x61, 0x57, 0x65, 0x69, 0x67, 0x68, 0x74, 0x42, 0x32, 0x5a, 0x30, 0x61,
	0x2e, 0x79, 0x61, 0x6e, 0x64, 0x65, 0x78, 0x2d, 0x74, 0x65, 0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f,
	0x79, 0x74, 0x2f, 0x67, 0x6f, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x63, 0x6c, 0x69, 0x65,
	0x6e, 0x74, 0x2f, 0x63, 0x68, 0x75, 0x6e, 0x6b, 0x5f, 0x63, 0x6c, 0x69, 0x65, 0x6e, 0x74,
}

var (
	file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDescOnce sync.Once
	file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDescData = file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDesc
)

func file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDescGZIP() []byte {
	file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDescOnce.Do(func() {
		file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDescData = protoimpl.X.CompressGZIP(file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDescData)
	})
	return file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDescData
}

var file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_msgTypes = make([]protoimpl.MessageInfo, 1)
var file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_goTypes = []interface{}{
	(*TDataStatistics)(nil), // 0: NYT.NChunkClient.NProto.TDataStatistics
}
var file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_depIdxs = []int32{
	0, // [0:0] is the sub-list for method output_type
	0, // [0:0] is the sub-list for method input_type
	0, // [0:0] is the sub-list for extension type_name
	0, // [0:0] is the sub-list for extension extendee
	0, // [0:0] is the sub-list for field type_name
}

func init() { file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_init() }
func file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_init() {
	if File_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TDataStatistics); i {
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
			RawDescriptor: file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   1,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_goTypes,
		DependencyIndexes: file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_depIdxs,
		MessageInfos:      file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_msgTypes,
	}.Build()
	File_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto = out.File
	file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_rawDesc = nil
	file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_goTypes = nil
	file_yt_yt_proto_yt_client_chunk_client_proto_data_statistics_proto_depIdxs = nil
}
