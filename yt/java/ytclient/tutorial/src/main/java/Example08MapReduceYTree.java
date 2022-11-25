import java.util.Iterator;

import tech.ytsaurus.client.YtClient;
import tech.ytsaurus.client.operations.MapReduceSpec;
import tech.ytsaurus.client.operations.Mapper;
import tech.ytsaurus.client.operations.MapperSpec;
import tech.ytsaurus.client.operations.Operation;
import tech.ytsaurus.client.operations.ReducerSpec;
import tech.ytsaurus.client.operations.ReducerWithKey;
import tech.ytsaurus.client.operations.Statistics;
import tech.ytsaurus.client.request.MapReduceOperation;
import tech.ytsaurus.core.cypress.YPath;
import tech.ytsaurus.core.operations.OperationContext;
import tech.ytsaurus.core.operations.Yield;
import tech.ytsaurus.ysontree.YTree;
import tech.ytsaurus.ysontree.YTreeMapNode;


public class Example08MapReduceYTree {

    private Example08MapReduceYTree() {
    }

    public static class SimpleMapper implements Mapper<YTreeMapNode, YTreeMapNode> {
        @Override
        public void map(YTreeMapNode entry, Yield<YTreeMapNode> yield, Statistics statistics,
                        OperationContext context) {
            String name = entry.getString("name");

            YTreeMapNode outputRow = YTree.builder().beginMap()
                    .key("name").value(name)
                    .key("name_length").value(name.length())
                    .buildMap();

            yield.yield(outputRow);
        }
    }

    public static class SimpleReducer implements ReducerWithKey<YTreeMapNode, YTreeMapNode, String> {
        @Override
        public String key(YTreeMapNode entry) {
            return entry.getString("name");
        }

        @Override
        public void reduce(String key, Iterator<YTreeMapNode> input, Yield<YTreeMapNode> yield, Statistics statistics) {
            // В reduce приходят все записи с общим reduce ключом, т.е. в нашем случае с общим полем `name'.

            int sumNameLength = 0;
            while (input.hasNext()) {
                YTreeMapNode node = input.next();
                sumNameLength += node.getInt("name_length");
            }

            YTreeMapNode outputRow = YTree.builder().beginMap()
                    .key("name").value(key)
                    .key("sum_name_length").value(sumNameLength)
                    .buildMap();

            yield.yield(outputRow);
        }
    }

    public static void main(String[] args) {
        YtClient client = YtClient.builder()
                .setCluster("freud")
                .build();
        YPath inputTable = YPath.simple("//home/tutorial/staff_unsorted");
        YPath outputTable = YPath.simple("//tmp/" + System.getProperty("user.name") + "-tutorial-emails");

        try (client) {
            Operation op = client.mapReduce(MapReduceOperation.builder()
                    .setSpec(MapReduceSpec.builder()
                            .setInputTables(inputTable)
                            .setOutputTables(outputTable)
                            .setReduceBy("name")
                            .setSortBy("name")
                            .setReducerSpec(new ReducerSpec(new SimpleReducer()))
                            .setMapperSpec(new MapperSpec(new SimpleMapper()))
                            .build())
                    .build()
            ).join();

            System.err.println("Operation was finished (OperationId: " + op.getId() + ")");
            System.err.println("Status: " + op.getStatus().join());
        }

        System.err.println(
                "Output table: https://yt.yandex-team.ru/freud/#page=navigation&offsetMode=row&path=" + outputTable
        );
    }
}
