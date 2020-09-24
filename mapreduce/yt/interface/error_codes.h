#pragma once

//
// generated by generate-error-codes.py
//

namespace NYT {
namespace NClusterErrorCodes {



// from ./core/misc/public.h

////////////////////////////////////////////////////////////////////////////////

    constexpr int OK       = 0;
    constexpr int Generic  = 1;
    constexpr int Canceled = 2;
    constexpr int Timeout  = 3;

////////////////////////////////////////////////////////////////////////////////




// from ./core/rpc/public.h
namespace NRpc {

////////////////////////////////////////////////////////////////////////////////

    constexpr int TransportError                = 100;
    constexpr int ProtocolError                 = 101;
    constexpr int NoSuchService                 = 102;
    constexpr int NoSuchMethod                  = 103;
    constexpr int Unavailable                   = 105;
    constexpr int PoisonPill                    = 106;
    constexpr int RequestQueueSizeLimitExceeded = 108;
    constexpr int AuthenticationError           = 109;
    constexpr int InvalidCsrfToken              = 110;
    constexpr int InvalidCredentials            = 111;
    constexpr int StreamingNotSupported         = 112;

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc



// from ./core/bus/public.h
namespace NBus {

////////////////////////////////////////////////////////////////////////////////

    constexpr int TransportError = 100;

////////////////////////////////////////////////////////////////////////////////

} // namespace NBus



// from ./client/scheduler/public.h
namespace NScheduler {

////////////////////////////////////////////////////////////////////////////////

    constexpr int NoSuchOperation                        = 200;
    constexpr int InvalidOperationState                  = 201;
    constexpr int TooManyOperations                      = 202;
    constexpr int NoSuchJob                              = 203;
    constexpr int OperationFailedOnJobRestart            = 210;
    constexpr int OperationFailedWithInconsistentLocking = 211;
    constexpr int OperationControllerCrashed             = 212;
    constexpr int TestingError                           = 213;

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler



// from ./client/table_client/public.h
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

    constexpr int SortOrderViolation     = 301;
    constexpr int InvalidDoubleValue     = 302;
    constexpr int IncomparableType       = 303;
    constexpr int UnhashableType         = 304;
    // E.g. name table with more than #MaxColumnId columns (may come from legacy chunks).
    constexpr int CorruptedNameTable     = 305;
    constexpr int UniqueKeyViolation     = 306;
    constexpr int SchemaViolation        = 307;
    constexpr int RowWeightLimitExceeded = 308;
    constexpr int InvalidColumnFilter    = 309;
    constexpr int InvalidColumnRenaming  = 310;
    constexpr int IncompatibleKeyColumns = 311;
    constexpr int ReaderDeadlineExpired  = 312;
    constexpr int TimestampOutOfRange    = 313;

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient



// from ./client/cypress_client/public.h
namespace NCypressClient {

////////////////////////////////////////////////////////////////////////////////

    constexpr int SameTransactionLockConflict       = 400;
    constexpr int DescendantTransactionLockConflict = 401;
    constexpr int ConcurrentTransactionLockConflict = 402;
    constexpr int PendingLockConflict               = 403;
    constexpr int LockDestroyed                     = 404;

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypressClient



// from ./core/ytree/public.h
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

    constexpr int ResolveError              = 500;
    constexpr int AlreadyExists             = 501;
    constexpr int MaxChildCountViolation    = 502;
    constexpr int MaxStringLengthViolation  = 503;
    constexpr int MaxAttributeSizeViolation = 504;
    constexpr int MaxKeyLengthViolation     = 505;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree



// from ./client/hydra/public.h
namespace NHydra {

////////////////////////////////////////////////////////////////////////////////

    constexpr int NoSuchSnapshot         = 600;
    constexpr int NoSuchChangelog        = 601;
    constexpr int InvalidEpoch           = 602;
    constexpr int InvalidVersion         = 603;
    constexpr int OutOfOrderMutations    = 609;
    constexpr int InvalidSnapshotVersion = 610;

////////////////////////////////////////////////////////////////////////////////

} // namespace NHydra



// from ./client/chunk_client/public.h
namespace NChunkClient {

////////////////////////////////////////////////////////////////////////////////

    constexpr int AllTargetNodesFailed      = 700;
    constexpr int SendBlocksFailed          = 701;
    constexpr int NoSuchSession             = 702;
    constexpr int SessionAlreadyExists      = 703;
    constexpr int ChunkAlreadyExists        = 704;
    constexpr int WindowError               = 705;
    constexpr int BlockContentMismatch      = 706;
    constexpr int NoSuchBlock               = 707;
    constexpr int NoSuchChunk               = 708;
    constexpr int NoLocationAvailable       = 710;
    constexpr int IOError                   = 711;
    constexpr int MasterCommunicationFailed = 712;
    constexpr int NoSuchChunkTree           = 713;
    constexpr int MasterNotConnected        = 714;
    constexpr int ChunkUnavailable          = 716;
    constexpr int NoSuchChunkList           = 717;
    constexpr int WriteThrottlingActive     = 718;
    constexpr int NoSuchMedium              = 719;
    constexpr int OptimisticLockFailure     = 720;
    constexpr int InvalidBlockChecksum      = 721;
    constexpr int BlockOutOfRange           = 722;
    constexpr int ObjectNotReplicated       = 723;
    constexpr int MissingExtension          = 724;
    constexpr int BandwidthThrottlingFailed = 725;
    constexpr int ReaderTimeout             = 726;
    constexpr int NoSuchChunkView           = 727;

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient



// from ./client/election/public.h
namespace NElection {

////////////////////////////////////////////////////////////////////////////////

    constexpr int InvalidState  = 800;
    constexpr int InvalidLeader = 801;
    constexpr int InvalidEpoch  = 802;

////////////////////////////////////////////////////////////////////////////////

} // namespace NElection



// from ./client/security_client/public.h
namespace NSecurityClient {

////////////////////////////////////////////////////////////////////////////////

    constexpr int AuthenticationError           = 900;
    constexpr int AuthorizationError            = 901;
    constexpr int AccountLimitExceeded          = 902;
    constexpr int UserBanned                    = 903;
    constexpr int RequestQueueSizeLimitExceeded = 904;
    constexpr int NoSuchAccount                 = 905;
    constexpr int SafeModeEnabled               = 906;

////////////////////////////////////////////////////////////////////////////////

} // namespace NSecurityClient



// from ./client/object_client/public.h
namespace NObjectClient {

////////////////////////////////////////////////////////////////////////////////

    constexpr int PrerequisiteCheckFailed = 1000;

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectClient



// from ./server/lib/exec_agent/public.h
namespace NExecAgent {

////////////////////////////////////////////////////////////////////////////////

    constexpr int ConfigCreationFailed           = 1100;
    constexpr int AbortByScheduler               = 1101;
    constexpr int ResourceOverdraft              = 1102;
    constexpr int WaitingJobTimeout              = 1103;
    constexpr int SlotNotFound                   = 1104;
    constexpr int JobEnvironmentDisabled         = 1105;
    constexpr int JobProxyConnectionFailed       = 1106;
    constexpr int ArtifactCopyingFailed          = 1107;
    constexpr int NodeDirectoryPreparationFailed = 1108;
    constexpr int SlotLocationDisabled           = 1109;
    constexpr int QuotaSettingFailed             = 1110;
    constexpr int RootVolumePreparationFailed    = 1111;
    constexpr int NotEnoughDiskSpace             = 1112;
    constexpr int ArtifactDownloadFailed         = 1113;
    constexpr int JobProxyPreparationTimeout     = 1114;
    constexpr int JobPreparationTimeout          = 1115;
    constexpr int JobProxyFailed                 = 1120;

////////////////////////////////////////////////////////////////////////////////

} // namespace NExecAgent



// from ./ytlib/job_proxy/public.h
namespace NJobProxy {

////////////////////////////////////////////////////////////////////////////////

    constexpr int MemoryLimitExceeded  = 1200;
    constexpr int MemoryCheckFailed    = 1201;
    constexpr int JobTimeLimitExceeded = 1202;
    constexpr int UnsupportedJobType   = 1203;
    constexpr int JobNotPrepared       = 1204;
    constexpr int UserJobFailed        = 1205;

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy



// from ./server/node/data_node/public.h
namespace NDataNode {

////////////////////////////////////////////////////////////////////////////////

    constexpr int LocalChunkReaderFailed = 1300;
    constexpr int LayerUnpackingFailed   = 1301;

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode



// from ./core/net/public.h
namespace NNet {

////////////////////////////////////////////////////////////////////////////////

    constexpr int Aborted         = 1500;
    constexpr int ResolveTimedOut = 1501;

////////////////////////////////////////////////////////////////////////////////

} // namespace NNet



// from ./client/node_tracker_client/public.h
namespace NNodeTrackerClient {

////////////////////////////////////////////////////////////////////////////////

    constexpr int NoSuchNode       = 1600;
    constexpr int InvalidState     = 1601;
    constexpr int NoSuchNetwork    = 1602;
    constexpr int NoSuchRack       = 1603;
    constexpr int NoSuchDataCenter = 1604;

////////////////////////////////////////////////////////////////////////////////

} // namespace NNodeTrackerClient



// from ./client/tablet_client/public.h
namespace NTabletClient {

////////////////////////////////////////////////////////////////////////////////

    constexpr int TransactionLockConflict   = 1700;
    constexpr int NoSuchTablet              = 1701;
    constexpr int TabletNotMounted          = 1702;
    constexpr int AllWritesDisabled         = 1703;
    constexpr int InvalidMountRevision      = 1704;
    constexpr int TableReplicaAlreadyExists = 1705;
    constexpr int InvalidTabletState        = 1706;

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletClient



// from ./server/lib/shell/public.h
namespace NShell {

////////////////////////////////////////////////////////////////////////////////

    constexpr int ShellExited          = 1800;
    constexpr int ShellManagerShutDown = 1801;

////////////////////////////////////////////////////////////////////////////////

} // namespace NShell



// from ./client/api/public.h
namespace NApi {

////////////////////////////////////////////////////////////////////////////////

    constexpr int TooManyConcurrentRequests = 1900;
    constexpr int JobArchiveUnavailable     = 1910;
    constexpr int OperationProgressOutdated = 1911;
    constexpr int NoSuchOperation           = 1915;

////////////////////////////////////////////////////////////////////////////////

} // namespace NApi



// from ./server/controller_agent/chunk_pools/public.h
namespace NChunkPools {

////////////////////////////////////////////////////////////////////////////////

    constexpr int DataSliceLimitExceeded             = 2000;
    constexpr int MaxDataWeightPerJobExceeded        = 2001;
    constexpr int MaxPrimaryDataWeightPerJobExceeded = 2002;

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkPools



// from ./client/api/rpc_proxy/public.h
namespace NApi {

////////////////////////////////////////////////////////////////////////////////

    constexpr int ProxyBanned = 2100;

////////////////////////////////////////////////////////////////////////////////

} // namespace NApi



// from ./ytlib/controller_agent/public.h
namespace NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

    constexpr int AgentCallFailed           = 4400;
    constexpr int NoOnlineNodeToScheduleJob = 4410;
    constexpr int MaterializationFailed     = 4415;

////////////////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent



// from ./client/transaction_client/public.h
namespace NTransactionClient {

////////////////////////////////////////////////////////////////////////////////

    constexpr int NoSuchTransaction = 11000;

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransactionClient



// from ./server/lib/containers/public.h
namespace NContainers {

////////////////////////////////////////////////////////////////////////////////

    constexpr int FailedToStartContainer = 13000;

////////////////////////////////////////////////////////////////////////////////

} // namespace NContainers



// from ./ytlib/job_prober_client/public.h
namespace NJobProberClient {

////////////////////////////////////////////////////////////////////////////////

    constexpr int JobIsNotRunning = 17000;

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProberClient

} // namespace NClusterErrorCodes
} // namespace NYT
