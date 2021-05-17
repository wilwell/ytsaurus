from base import ClickHouseTestBase, Clique, QueryFailedError

from yt_commands import (create, write_table, authors, raises_yt_error, print_debug, get, create_dynamic_table,
                         insert_rows, sync_mount_table)

from yt.packages.six.moves import map as imap

import random
import copy


class TestJoinAndIn(ClickHouseTestBase):
    def setup(self):
        self._setup()

    @authors("max42")
    def test_global_join(self):
        create(
            "table",
            "//tmp/t1",
            attributes={"schema": [{"name": "a", "type": "int64"}, {"name": "b", "type": "string"}]},
        )
        create(
            "table",
            "//tmp/t2",
            attributes={"schema": [{"name": "c", "type": "int64"}, {"name": "d", "type": "string"}]},
        )
        create(
            "table",
            "//tmp/t3",
            attributes={"schema": [{"name": "a", "type": "int64"}, {"name": "e", "type": "double"}]},
        )
        write_table("//tmp/t1", [{"a": 42, "b": "qwe"}, {"a": 27, "b": "xyz"}])
        write_table("//tmp/t2", [{"c": 42, "d": "asd"}, {"c": -1, "d": "xyz"}])
        write_table("//tmp/t3", [{"a": 42, "e": 3.14}, {"a": 27, "e": 2.718}])
        with Clique(1) as clique:
            expected = [{"a": 42, "b": "qwe", "c": 42, "d": "asd"}]
            assert clique.make_query('select * from "//tmp/t1" t1 global join "//tmp/t2" t2 on t1.a = t2.c') == expected
            assert clique.make_query('select * from "//tmp/t1" t1 global join "//tmp/t2" t2 on t2.c = t1.a') == expected

            expected_on = [{"a": 27, "b": "xyz", "t3.a": 27, "e": 2.718}, {"a": 42, "b": "qwe", "t3.a": 42, "e": 3.14}]
            expected_using = [{"a": 27, "b": "xyz", "e": 2.718}, {"a": 42, "b": "qwe", "e": 3.14}]
            assert clique.make_query('select * from "//tmp/t1" t1 global join "//tmp/t3" t3'
                                     ' using a order by a') == expected_using
            assert clique.make_query('select * from "//tmp/t1" t1 global join "//tmp/t3" t3'
                                     ' on t1.a = t3.a order by a') == expected_on
            assert clique.make_query('select * from "//tmp/t1" t1 global join "//tmp/t3" t3 '
                                     'on t3.a = t1.a order by a') == expected_on
            assert clique.make_query('select * from "//tmp/t1" t1 global join "//tmp/t3" t3 '
                                     'on t1.a = t3.a order by t1.a') == expected_on
            assert clique.make_query('select * from "//tmp/t1" t1 global join "//tmp/t3" t3 '
                                     'on t3.a = t1.a order by t3.a') == expected_on

            assert clique.make_query('select * from "//tmp/t1" global join "//tmp/t2" on a = c') == expected
            assert clique.make_query('select * from "//tmp/t1" global join "//tmp/t2" on c = a') == expected
            assert (
                clique.make_query('select * from "//tmp/t1" global join "//tmp/t3" using a order by a')
                == expected_using
            )

    @authors("max42")
    def test_global_in(self):
        create("table", "//tmp/t1", attributes={"schema": [{"name": "a", "type": "int64", "required": True}]})
        create("table", "//tmp/t2", attributes={"schema": [{"name": "a", "type": "int64", "required": True}]})
        write_table("//tmp/t1", [{"a": 1}, {"a": 3}, {"a": -42}])
        write_table("//tmp/t2", [{"a": 5}, {"a": 42}, {"a": 3}, {"a": 1}])
        with Clique(1) as clique:
            expected = [{"a": 1}, {"a": 3}]
            assert clique.make_query('select a from "//tmp/t1" where a global in '
                                     '(select * from "//tmp/t2") order by a') == expected
            assert clique.make_query('select a from "//tmp/t2" where a global in '
                                     '(select * from "//tmp/t1") order by a') == expected

            assert clique.make_query('select toInt64(42) global in (select * from "//tmp/t2")')[0].values() == [1]
            assert clique.make_query('select toInt64(43) global in (select * from "//tmp/t2")')[0].values() == [0]

            assert clique.make_query('select toInt64(42) global in (select * from "//tmp/t2") from "//tmp/t1" limit 1')[
                0
            ].values() == [1]
            assert clique.make_query('select toInt64(43) global in (select * from "//tmp/t2") from "//tmp/t1" limit 1')[
                0
            ].values() == [0]

    @authors("max42")
    def test_sorted_join_simple(self):
        create(
            "table",
            "//tmp/t1",
            attributes={
                "schema": [
                    {"name": "key", "type": "int64", "required": True, "sort_order": "ascending"},
                    {"name": "lhs", "type": "string", "required": True},
                ]
            },
        )
        create(
            "table",
            "//tmp/t2",
            attributes={
                "schema": [
                    {"name": "key", "type": "int64", "required": True, "sort_order": "ascending"},
                    {"name": "rhs", "type": "string", "required": True},
                ]
            },
        )
        lhs_rows = [
            [{"key": 1, "lhs": "foo1"}],
            [{"key": 2, "lhs": "foo2"}, {"key": 3, "lhs": "foo3"}],
            [{"key": 4, "lhs": "foo4"}],
        ]
        rhs_rows = [
            [{"key": 1, "rhs": "bar1"}, {"key": 2, "rhs": "bar2"}],
            [{"key": 3, "rhs": "bar3"}, {"key": 4, "rhs": "bar4"}],
        ]

        for rows in lhs_rows:
            write_table("<append=%true>//tmp/t1", rows)
        for rows in rhs_rows:
            write_table("<append=%true>//tmp/t2", rows)

        with Clique(3) as clique:
            expected = [
                {"key": 1, "lhs": "foo1", "rhs": "bar1"},
                {"key": 2, "lhs": "foo2", "rhs": "bar2"},
                {"key": 3, "lhs": "foo3", "rhs": "bar3"},
                {"key": 4, "lhs": "foo4", "rhs": "bar4"},
            ]
            assert clique.make_query('select key, lhs, rhs from "//tmp/t1" t1 join "//tmp/t2" t2 '
                                     'using key order by key') == expected
            assert clique.make_query(
                'select key, lhs, rhs from "//tmp/t1" t1 join "//tmp/t2" t2 on t1.key = t2.key order by key'
            ) == expected

    @authors("max42")
    def test_right_or_full_join_simple(self):
        create(
            "table",
            "//tmp/t1",
            attributes={
                "schema": [
                    {"name": "key", "type": "int64", "required": True, "sort_order": "ascending"},
                    {"name": "lhs", "type": "string", "required": True},
                ]
            },
        )
        create(
            "table",
            "//tmp/t2",
            attributes={
                "schema": [
                    {"name": "key", "type": "int64", "required": True, "sort_order": "ascending"},
                    {"name": "rhs", "type": "string", "required": True},
                ]
            },
        )
        lhs_rows = [
            {"key": 0, "lhs": "foo0"},
            {"key": 1, "lhs": "foo1"},
            {"key": 3, "lhs": "foo3"},
            {"key": 7, "lhs": "foo7"},
            {"key": 8, "lhs": "foo8"},
        ]
        rhs_rows = [
            {"key": 0, "rhs": "bar0"},
            {"key": 0, "rhs": "bar0"},
            {"key": 2, "rhs": "bar2"},
            {"key": 4, "rhs": "bar4"},
            {"key": 9, "rhs": "bar9"},
        ]

        for row in lhs_rows:
            write_table("<append=%true>//tmp/t1", [row])
        for row in rhs_rows:
            write_table("<append=%true>//tmp/t2", [row])

        with Clique(2) as clique:
            expected_right = [
                {"key": 0, "lhs": "foo0", "rhs": "bar0"},
                {"key": 0, "lhs": "foo0", "rhs": "bar0"},
                {"key": 2, "lhs": None, "rhs": "bar2"},
                {"key": 4, "lhs": None, "rhs": "bar4"},
                {"key": 9, "lhs": None, "rhs": "bar9"},
            ]
            expected_full = [
                {"key": 0, "lhs": "foo0", "rhs": "bar0"},
                {"key": 0, "lhs": "foo0", "rhs": "bar0"},
                {"key": 1, "lhs": "foo1", "rhs": None},
                {"key": 2, "lhs": None, "rhs": "bar2"},
                {"key": 3, "lhs": "foo3", "rhs": None},
                {"key": 4, "lhs": None, "rhs": "bar4"},
                {"key": 7, "lhs": "foo7", "rhs": None},
                {"key": 8, "lhs": "foo8", "rhs": None},
                {"key": 9, "lhs": None, "rhs": "bar9"},
            ]
            assert (
                clique.make_query(
                    'select key, lhs, rhs from "//tmp/t1" t1 global right join "//tmp/t2" t2 using key order by key'
                )
                == expected_right
            )
            assert (
                clique.make_query(
                    'select key, lhs, rhs from "//tmp/t1" t1 global full join "//tmp/t2" t2 using key order by key'
                )
                == expected_full
            )

    @authors("max42")
    def test_sorted_join_stress(self):
        create(
            "table",
            "//tmp/t1",
            attributes={
                "schema": [
                    {"name": "key", "type": "int64", "required": True, "sort_order": "ascending"},
                    {"name": "lhs", "type": "string", "required": True},
                ]
            },
        )
        create(
            "table",
            "//tmp/t2",
            attributes={
                "schema": [
                    {"name": "key", "type": "int64", "required": True, "sort_order": "ascending"},
                    {"name": "rhs", "type": "string", "required": True},
                ]
            },
        )
        rnd = random.Random(x=42)

        # Small values (uncomment for debugging):
        # row_count, key_range, chunk_count = 5, 7, 2
        # Large values:
        row_count, key_range, chunk_count = 300, 500, 15

        def generate_rows(row_count, key_range, chunk_count, payload_column_name, payload_value):
            keys = [rnd.randint(0, key_range - 1) for _ in range(row_count)]
            keys = sorted(keys)
            delimiters = [0] + sorted([rnd.randint(0, row_count) for _ in range(chunk_count - 1)]) + [row_count]
            rows = []
            for i in range(chunk_count):
                rows.append(
                    [
                        {"key": key, payload_column_name: "%s%03d" % (payload_value, key)}
                        for key in keys[delimiters[i]:delimiters[i + 1]]
                    ]
                )
            return rows

        def write_multiple_chunks(path, row_batches):
            for row_batch in row_batches:
                write_table("<append=%true>" + path, row_batch)
            print_debug("Table {}".format(path))
            chunk_ids = get(path + "/@chunk_ids", verbose=False)
            for chunk_id in chunk_ids:
                attrs = get("#" + chunk_id + "/@", attributes=["min_key", "max_key", "row_count"], verbose=False)
                print_debug(
                    "{}: rc = {}, bk = ({}, {})".format(
                        chunk_id, attrs["row_count"], attrs["min_key"], attrs["max_key"]
                    )
                )

        lhs_rows = generate_rows(row_count, key_range, chunk_count, "lhs", "foo")
        rhs_rows = generate_rows(row_count, key_range, chunk_count, "rhs", "bar")

        write_multiple_chunks("//tmp/t1", lhs_rows)
        write_multiple_chunks("//tmp/t2", rhs_rows)

        lhs_rows = sum(lhs_rows, [])
        rhs_rows = sum(rhs_rows, [])

        def expected_result(kind, key_range, lhs_rows, rhs_rows):
            it_lhs = 0
            it_rhs = 0
            result = []
            for key in range(key_range):
                start_lhs = it_lhs
                while it_lhs < len(lhs_rows) and lhs_rows[it_lhs]["key"] == key:
                    it_lhs += 1
                finish_lhs = it_lhs
                start_rhs = it_rhs
                while it_rhs < len(rhs_rows) and rhs_rows[it_rhs]["key"] == key:
                    it_rhs += 1
                finish_rhs = it_rhs
                maybe_lhs_null = []
                maybe_rhs_null = []
                if start_lhs == finish_lhs and start_rhs == finish_rhs:
                    continue
                if kind in ("right", "full") and start_lhs == finish_lhs:
                    maybe_lhs_null.append({"key": key, "lhs": None})
                if kind in ("left", "full") and start_rhs == finish_rhs:
                    maybe_rhs_null.append({"key": key, "rhs": None})
                for lhs_row in lhs_rows[start_lhs:finish_lhs] + maybe_lhs_null:
                    for rhs_row in rhs_rows[start_rhs:finish_rhs] + maybe_rhs_null:
                        result.append({"key": key, "lhs": lhs_row["lhs"], "rhs": rhs_row["rhs"]})
            return result

        expected_results = {}
        for kind in ("inner", "left", "right", "full"):
            expected_results[kind] = expected_result(kind, key_range, lhs_rows, rhs_rows)

        # Transform unicode strings into non-unicode.
        def sanitize(obj):
            if type(obj) == unicode:
                return str(obj)
            else:
                return obj

        # Same for dictionaries
        def sanitize_dict(unicode_dict):
            return dict([(sanitize(key), sanitize(value)) for key, value in unicode_dict.iteritems()])

        index = 0
        for instance_count in range(1, 5):
            with Clique(instance_count) as clique:
                # TODO(max42): CHYT-390.
                for lhs_arg in ('"//tmp/t1"', '(select * from "//tmp/t1")'):
                    for rhs_arg in ('"//tmp/t2"', '(select * from "//tmp/t2")'):
                        for globalness in ("", "global"):
                            for kind in ("inner", "left", "right", "full"):
                                query = (
                                    "select key, lhs, rhs from {lhs_arg} l {globalness} {kind} join {rhs_arg} r "
                                    "using key order by key, lhs, rhs nulls first".format(**locals())
                                )
                                result = list(imap(sanitize_dict, clique.make_query(query, verbose=False)))

                                expected = expected_results[kind]
                                print_debug(
                                    "Query #{}: '{}' produced {} rows, expected {} rows".format(
                                        index, query, len(result), len(expected)
                                    )
                                )
                                index += 1
                                result = list(imap(str, result))
                                expected = list(imap(str, expected))
                                if result != expected:
                                    print_debug("Produced:")
                                    for row in result:
                                        char = "+" if row not in expected else " "
                                        print_debug(char + " " + row)
                                    print_debug("Expected:")
                                    for row in expected:
                                        char = "-" if row not in result else " "
                                        print_debug(char + " " + row)
                                    assert False

    @authors("max42")
    def test_tricky_join(self):
        # CHYT-240.
        create(
            "table", "//tmp/t1", attributes={"schema": [{"name": "key", "type": "int64", "sort_order": "ascending"}]}
        )
        create(
            "table", "//tmp/t2", attributes={"schema": [{"name": "key", "type": "int64", "sort_order": "ascending"}]}
        )
        write_table("//tmp/t1", [{"key": 0}, {"key": 1}])
        write_table("<append=%true>//tmp/t1", [{"key": 2}, {"key": 3}])
        write_table("//tmp/t2", [{"key": 0}, {"key": 1}])
        write_table("<append=%true>//tmp/t2", [{"key": 4}, {"key": 5}])
        write_table("<append=%true>//tmp/t2", [{"key": 6}, {"key": 7}])
        write_table("<append=%true>//tmp/t2", [{"key": 8}, {"key": 9}])
        with Clique(2, config_patch={"yt": {"subquery": {"min_data_weight_per_thread": 5000}}}) as clique:
            assert clique.make_query('select * from "//tmp/t1" t1 join "//tmp/t2" t2 using key') == [
                {"key": 0},
                {"key": 1},
            ]

    @authors("max42")
    def test_sorted_join_with_wider_key_condition(self):
        # CHYT-487.
        create("table", "//tmp/t1", attributes={"schema": [
            {"name": "key", "type": "int64", "sort_order": "ascending"}
        ]})
        create("table", "//tmp/t2", attributes={"schema": [
            {"name": "key", "type": "int64", "sort_order": "ascending"},
            {"name": "subkey", "type": "int64", "sort_order": "ascending"}
        ]})
        write_table("//tmp/t1", [{"key": 1}, {"key": 5}, {"key": 7}, {"key": 10}])
        write_table("<append=%true>//tmp/t2", [{"key": 5, "subkey": 42}])
        write_table("<append=%true>//tmp/t2", [{"key": 7, "subkey": -1}])
        write_table("<append=%true>//tmp/t2", [{"key": 10, "subkey": 42}])
        with Clique(1) as clique:
            assert clique.make_query_and_validate_row_count(
                'select * from "//tmp/t1" t1 inner join "//tmp/t2" t2 using key where t2.subkey == 42',
                exact=6) == [{"key": 5, "subkey": 42},
                             {"key": 10, "subkey": 42}]

    @authors("max42")
    def test_join_under_different_names(self):
        # CHYT-270.
        create(
            "table", "//tmp/t1", attributes={"schema": [{"name": "key1", "type": "int64", "sort_order": "ascending"}]}
        )
        create(
            "table", "//tmp/t2", attributes={"schema": [{"name": "key2", "type": "int64", "sort_order": "ascending"}]}
        )
        for i in range(3):
            write_table("<append=%true>//tmp/t1", [{"key1": i}])
            write_table("<append=%true>//tmp/t2", [{"key2": i}])
        with Clique(1) as clique:
            assert (
                len(
                    clique.make_query(
                        'select * from "//tmp/t1" A inner join "//tmp/t2" B on A.key1 = B.key2 where '
                        "A.key1 in (1, 2) and B.key2 in (2, 3)"
                    )
                )
                == 1
            )

    @authors("max42")
    def test_tricky_join2(self):
        # CHYT-273.
        create(
            "table", "//tmp/t1", attributes={"schema": [{"name": "key", "type": "int64", "sort_order": "ascending"}]}
        )
        create(
            "table", "//tmp/t2", attributes={"schema": [{"name": "key", "type": "int64", "sort_order": "ascending"}]}
        )
        write_table("//tmp/t1", [{"key": 0}, {"key": 1}])
        write_table("//tmp/t2", [{"key": 4}, {"key": 5}])
        with Clique(1) as clique:
            assert (
                len(
                    clique.make_query(
                        'select * from "//tmp/t1" A inner join "//tmp/t2" B on A.key = B.key where ' "A.key = 1"
                    )
                )
                == 0
            )

    @authors("max42")
    def test_forbidden_non_primitive_join(self):
        # CHYT-323.
        create(
            "table", "//tmp/t1", attributes={"schema": [{"name": "key", "type": "int64", "sort_order": "ascending"}]}
        )
        create(
            "table", "//tmp/t2", attributes={"schema": [{"name": "key", "type": "int64", "sort_order": "ascending"}]}
        )
        write_table("//tmp/t1", [{"key": 42}])
        write_table("//tmp/t2", [{"key": 43}])
        with Clique(1) as clique:
            with raises_yt_error(QueryFailedError):
                clique.make_query('select * from "//tmp/t1" A inner join "//tmp/t2" B on A.key + 1 = B.key')

    @authors("max42")
    def test_cross_join(self):
        # CHYT-445.
        create(
            "table",
            "//tmp/t1_sorted",
            attributes={"schema": [{"name": "key1", "type": "int64", "sort_order": "ascending"}]},
        )
        create("table", "//tmp/t1_not_sorted", attributes={"schema": [{"name": "key1", "type": "int64"}]})
        create(
            "table", "//tmp/t2", attributes={"schema": [{"name": "key2", "type": "int64", "sort_order": "ascending"}]}
        )
        write_table("<append=%true>//tmp/t1_sorted", [{"key1": 1}])
        write_table("<append=%true>//tmp/t1_sorted", [{"key1": 2}])
        write_table("<append=%true>//tmp/t1_not_sorted", [{"key1": 1}])
        write_table("<append=%true>//tmp/t1_not_sorted", [{"key1": 2}])
        write_table("//tmp/t2", [{"key2": 3}, {"key2": 4}])

        def expected_result(left_rows):
            result = []
            for row in left_rows:
                for key2 in (3, 4):
                    result_row = copy.deepcopy(row)
                    result_row["key2"] = key2
                    result.append(result_row)
            return result

        with Clique(1) as clique:
            for tp in ("sorted", "not_sorted"):
                assert clique.make_query(
                    "select * from `//tmp/t1_{}` t1 cross join `//tmp/t2` t2 order by (key1, key2)".format(tp)
                ) == expected_result([{"key1": 1}, {"key1": 2}])
                assert clique.make_query(
                    "select * from `//tmp/t1_{}` t1 cross join `//tmp/t2` t2 "
                    "where key1 == 1 order by (key1, key2)".format(tp)
                ) == expected_result([{"key1": 1}])

    @authors("max42")
    def test_join_dynamic_tables_with_dynamic_stores(self):
        # CHYT-547.
        create_dynamic_table("//tmp/t1", schema=[{"name": "k", "type": "int64", "sort_order": "ascending"},
                                                 {"name": "v", "type": "string"}], enable_dynamic_store_read=True)
        create_dynamic_table("//tmp/t2", schema=[{"name": "k", "type": "int64", "sort_order": "ascending"},
                                                 {"name": "v", "type": "string"}], enable_dynamic_store_read=True)
        sync_mount_table("//tmp/t1")
        sync_mount_table("//tmp/t2")
        insert_rows("//tmp/t1", [{"k": 1, "v": "a1"}, {"k": 3, "v": "a3"}, {"k": 4, "v": "a4"}, {"k": 7, "v": "a7"}])
        insert_rows("//tmp/t2", [{"k": 2, "v": "b2"}, {"k": 3, "v": "b3"}, {"k": 6, "v": "b6"}, {"k": 7, "v": "b7"}])
        with Clique(1,
                    config_patch={
                        "yt": {"enable_dynamic_tables": True},
                    }) as clique:
            assert clique.make_query("select k, t1.v v1, t2.v v2 from `//tmp/t1` t1 join `//tmp/t2` t2 using k") == [
                {"k": 3, "v1": "a3", "v2": "b3"}, {"k": 7, "v1": "a7", "v2": "b7"},
            ]
