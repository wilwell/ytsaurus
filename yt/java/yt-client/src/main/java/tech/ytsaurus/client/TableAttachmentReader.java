package tech.ytsaurus.client;

import java.util.Arrays;
import java.util.List;

import javax.annotation.Nullable;

import NYT.NChunkClient.NProto.DataStatistics.TDataStatistics;
import tech.ytsaurus.core.tables.TableSchema;

public interface TableAttachmentReader<T> {
    TableAttachmentReader<byte[]> BYPASS = new TableAttachmentReader<byte[]>() {
        @Override
        public List<byte[]> parse(byte[] attachments) {
            if (attachments == null) {
                return null;
            } else {
                return Arrays.asList(attachments);
            }
        }

        @Override
        public List<byte[]> parse(byte[] attachments, int offset, int length) {
            if (attachments == null) {
                return null;
            } else {
                if (offset == 0 && length == attachments.length) {
                    return Arrays.asList(attachments);
                } else {
                    return Arrays.asList(Arrays.copyOfRange(attachments, offset, length));
                }
            }
        }

        @Override
        public long getTotalRowCount() {
            return 0;
        }

        @Override
        public TDataStatistics getDataStatistics() {
            return null;
        }

        @Nullable
        @Override
        public TableSchema getCurrentReadSchema() {
            return null;
        }
    };

    List<T> parse(byte[] attachments) throws Exception;

    List<T> parse(byte[] attachments, int offset, int length) throws Exception;

    long getTotalRowCount();

    @Nullable
    TDataStatistics getDataStatistics();

    @Nullable
    TableSchema getCurrentReadSchema();
}
