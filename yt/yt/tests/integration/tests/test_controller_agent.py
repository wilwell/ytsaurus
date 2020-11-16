from yt_env_setup import (
    YTEnvSetup,
    wait,
    is_asan_build,
    Restarter,
    CONTROLLER_AGENTS_SERVICE,
)
from yt_commands import *
from yt_helpers import *

from yt.yson import *

import pytest
from flaky import flaky

import time
from collections import defaultdict

import __builtin__

##################################################################


class TestControllerAgentOrchid(YTEnvSetup):
    NUM_SCHEDULERS = 1

    @authors("levysotsky")
    def test_controller_agent_orchid(self):
        controller_agents = ls("//sys/controller_agents/instances")
        assert len(controller_agents) == 1

        controller_agent_orchid = "//sys/controller_agents/instances/{}/orchid/controller_agent".format(
            controller_agents[0]
        )

        wait(lambda: exists(controller_agent_orchid + "/incarnation_id"))


class TestControllerMemoryUsage(YTEnvSetup):
    NUM_SCHEDULERS = 1

    DELTA_CONTROLLER_AGENT_CONFIG = {
        "controller_agent": {
            "tagged_memory_statistics_update_period": 100,
        }
    }

    @authors("ignat")
    @pytest.mark.skipif(is_asan_build(), reason="Memory allocation is not reported under ASAN")
    def test_controller_memory_usage(self):
        # In this test we rely on the assignment order of memory tags.
        # Tags are given to operations sequentially during lifetime of controller agent.
        # So this test should pass only if it is the first test in this test suite.

        create("table", "//tmp/t_in")
        create("table", "//tmp/t_out")

        write_table("<append=%true>//tmp/t_in", [{"a": 0}])

        controller_agents = ls("//sys/controller_agents/instances")
        assert len(controller_agents) == 1

        controller_agent_orchid = "//sys/controller_agents/instances/{}/orchid/controller_agent".format(
            controller_agents[0]
        )

        def check(tag_number, operation, usage_lower_bound, usage_upper_bound):
            state = operation.get_state()
            if state != "running":
                return False
            statistics = get(controller_agent_orchid + "/tagged_memory_statistics/" + tag_number)
            if statistics["operation_id"] == YsonEntity():
                return False
            assert statistics["operation_id"] == operation.id
            assert statistics["usage"] > usage_lower_bound
            assert statistics["usage"] < usage_upper_bound
            return True

        for entry in get(controller_agent_orchid + "/tagged_memory_statistics", verbose=False):
            assert entry["operation_id"] == YsonEntity()
            assert not entry["alive"]

        op_small = map(
            track=False,
            in_="//tmp/t_in",
            out="<sorted_by=[a]>//tmp/t_out",
            command="sleep 3600",
        )

        small_usage_path = op_small.get_path() + "/controller_orchid/memory_usage"
        wait(lambda: exists(small_usage_path))
        usage = get(small_usage_path)

        print_debug("small_usage =", usage)
        assert usage < 4 * 10 ** 6

        wait(lambda: check("0", op_small, 0, 4 * 10 ** 6))

        op_small.abort()

        op_large = map(
            track=False,
            in_="//tmp/t_in",
            out="<sorted_by=[a]>//tmp/t_out",
            command="sleep 3600",
            spec={
                "testing": {
                    "allocation_size": 20 * 1024 * 1024,
                }
            },
        )

        large_usage_path = op_large.get_path() + "/controller_orchid/memory_usage"
        wait(lambda: exists(large_usage_path))
        usage = get(large_usage_path)

        print_debug("large_usage =", usage)
        assert usage > 10 * 10 ** 6
        wait(
            lambda: get_operation(op_large.id, attributes=["memory_usage"], include_runtime=True)["memory_usage"]
            > 10 * 10 ** 6
        )

        wait(lambda: check("1", op_large, 10 * 10 ** 6, 30 * 10 ** 6))

        op_large.abort()

        time.sleep(5)

        for i, entry in enumerate(get(controller_agent_orchid + "/tagged_memory_statistics", verbose=False)):
            if i <= 1:
                assert entry["operation_id"] in (op_small.id, op_large.id)
            else:
                assert entry["operation_id"] == YsonEntity()
            assert not entry["alive"]


class TestControllerAgentMemoryPickStrategy(YTEnvSetup):
    NUM_SCHEDULERS = 1
    NUM_CONTROLLER_AGENTS = 2

    DELTA_SCHEDULER_CONFIG = {
        "scheduler": {
            "controller_agent_tracker": {
                "agent_pick_strategy": "memory_usage_balanced",
                "min_agent_available_memory": 0,
                "min_agent_available_memory_fraction": 0.0,
            }
        }
    }
    DELTA_CONTROLLER_AGENT_CONFIG = {
        "controller_agent": {
            "tagged_memory_statistics_update_period": 100,
        }
    }

    DELTA_NODE_CONFIG = {"exec_agent": {"job_controller": {"resource_limits": {"user_slots": 100, "cpu": 100}}}}

    @classmethod
    def modify_controller_agent_config(cls, config):
        if not hasattr(cls, "controller_agent_counter"):
            cls.controller_agent_counter = 0
        cls.controller_agent_counter += 1
        if cls.controller_agent_counter > 2:
            cls.controller_agent_counter -= 2
        config["controller_agent"]["total_controller_memory_limit"] = cls.controller_agent_counter * 50 * 1024 ** 2

    @authors("ignat")
    @flaky(max_runs=5)
    @pytest.mark.skipif(is_asan_build(), reason="Memory allocation is not reported under ASAN")
    def test_strategy(self):
        create("table", "//tmp/t_in", attributes={"replication_factor": 1})
        write_table("<append=%true>//tmp/t_in", [{"a": 0}])

        ops = []
        for i in xrange(45):
            out = "//tmp/t_out" + str(i)
            create("table", out, attributes={"replication_factor": 1})
            op = map(
                command="sleep 1000",
                in_="//tmp/t_in",
                out=out,
                spec={
                    "testing": {
                        "allocation_size": 1024 ** 2,
                    }
                },
                track=False,
            )
            wait(lambda: op.get_state() == "running")
            ops.append(op)

        address_to_operation = defaultdict(list)
        for op in ops:
            address_to_operation[get(op.get_path() + "/@controller_agent_address")].append(op.id)

        operation_balance = sorted(__builtin__.map(lambda value: len(value), address_to_operation.values()))
        balance_ratio = float(operation_balance[0]) / operation_balance[1]
        print_debug("BALANCE_RATIO", balance_ratio)
        if not (0.5 <= balance_ratio <= 0.8):
            for op in ops:
                print_debug(
                    op.id,
                    get(op.get_path() + "/controller_orchid/memory_usage", verbose=False),
                )
        assert 0.5 <= balance_ratio <= 0.8


##################################################################


class TestSchedulerControllerThrottling(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 5
    NUM_SCHEDULERS = 1

    DELTA_SCHEDULER_CONFIG = {"scheduler": {"schedule_job_time_limit": 100, "operations_update_period": 10}}

    @authors("ignat")
    def test_time_based_throttling(self):
        create("table", "//tmp/input")

        testing_options = {"scheduling_delay": 200}

        data = [{"foo": i} for i in range(5)]
        write_table("//tmp/input", data)

        create("table", "//tmp/output")
        op = map(
            track=False,
            in_="//tmp/input",
            out="//tmp/output",
            command="cat",
            spec={"testing": testing_options},
        )

        def check():
            jobs = get(op.get_path() + "/@progress/jobs", default=None)
            if jobs is None:
                return False
            # Progress is updates by controller, but abort is initiated by scheduler after job was scheduled.
            # Therefore races are possible.
            if jobs["running"] > 0:
                return False

            assert jobs["running"] == 0
            assert jobs["completed"]["total"] == 0
            return jobs["aborted"]["non_scheduled"]["scheduling_timeout"] > 0

        wait(check)


##################################################################


class TestCustomControllerQueues(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    DELTA_CONTROLLER_AGENT_CONFIG = {
        "controller_agent": {
            "schedule_job_controller_queue": "schedule_job",
            "build_job_spec_controller_queue": "build_job_spec",
            "job_events_controller_queue": "job_events",
        }
    }

    @authors("ignat")
    def test_run_map(self):
        data = [{"foo": i} for i in xrange(3)]
        create("table", "//tmp/in")
        create("table", "//tmp/out")
        write_table("//tmp/in", data)

        map(
            command="sleep 10",
            in_="//tmp/in",
            out="//tmp/out",
            spec={"data_size_per_job": 1, "locality_timeout": 0},
        )

    @authors("eshcherbin")
    def test_run_map_reduce(self):
        data = [{"foo": i} for i in xrange(3)]
        create("table", "//tmp/in")
        create("table", "//tmp/out")
        write_table("//tmp/in", data)

        map_reduce(
            mapper_command="cat",
            reducer_command="cat",
            in_="//tmp/in",
            out="//tmp/out",
            sort_by=["foo"],
        )

    @authors("eshcherbin")
    def test_run_merge_erase(self):
        data = [{"foo": i} for i in xrange(3)]
        create("table", "//tmp/in")
        create("table", "//tmp/out")
        write_table("//tmp/in", data)

        merge(in_="//tmp/in", out="//tmp/out", spec={"force_transform": True})
        erase("//tmp/in")

    @authors("eshcherbin")
    def test_run_reduce(self):
        data = [{"foo": i} for i in xrange(3)]
        create("table", "//tmp/in")
        create("table", "//tmp/out")
        write_table("//tmp/in", data, sorted_by=["foo"])

        reduce(command="sleep 1; cat", in_="//tmp/in", out="//tmp/out", reduce_by=["foo"])


##################################################################


class TestGetJobSpecFailed(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    @authors("ignat")
    def test_get_job_spec_failed(self):
        create("table", "//tmp/t_input")
        create("table", "//tmp/t_output")

        write_table("<append=%true>//tmp/t_input", [{"key": i} for i in xrange(2)])

        op = map(
            command="sleep 100",
            in_="//tmp/t_input",
            out="//tmp/t_output",
            spec={
                "testing": {"fail_get_job_spec": True},
            },
            track=False,
        )

        def check():
            jobs = get(op.get_path() + "/controller_orchid/progress/jobs", default=None)
            if jobs is None:
                return False
            return jobs["aborted"]["non_scheduled"]["get_spec_failed"] > 0

        wait(check)


##################################################################


class TestControllerAgentTags(YTEnvSetup):
    NUM_SCHEDULERS = 1
    NUM_CONTROLLER_AGENTS = 3

    @classmethod
    def modify_controller_agent_config(cls, config):
        if not hasattr(cls, "controller_agent_counter"):
            cls.controller_agent_counter = 0
        controller_agent_tag = None
        if cls.controller_agent_counter == 0:
            controller_agent_tag = "foo"
        elif cls.controller_agent_counter == 1:
            controller_agent_tag = "bar"
        cls.controller_agent_counter += 1
        if controller_agent_tag is not None:
            config["controller_agent"]["tags"] = [controller_agent_tag]
        config["controller_agent"]["controller_static_orchid_update_period"] = 100

    @authors("gritukan")
    def test_controller_agent_tags(self):
        foo_agent = None
        bar_agent = None
        default_agent = None

        def wait_for_tags_loaded():
            for agent in get("//sys/controller_agents/instances").keys():
                tags_path = "//sys/controller_agents/instances/{}/orchid/controller_agent/tags".format(agent)
                wait(lambda: len(get(tags_path)) > 0)

        wait_for_tags_loaded()
        for agent in get("//sys/controller_agents/instances").keys():
            agent_tags = get("//sys/controller_agents/instances/{}/orchid/controller_agent/tags".format(agent))
            assert len(agent_tags) == 1
            agent_tag = agent_tags[0]
            if agent_tag == "foo":
                foo_agent = agent
            elif agent_tag == "bar":
                bar_agent = agent
            else:
                assert agent_tag == "default"
                default_agent = agent

        assert foo_agent is not None
        assert bar_agent is not None
        assert default_agent is not None

        def get_controller_agent(op):
            attr_path = op.get_path() + "/@controller_agent_address"
            return get(attr_path, default=None)

        def run_with_tag(tag):
            if tag is None:
                spec = {}
            else:
                spec = {"controller_agent_tag": tag}
            return run_test_vanilla("sleep 1000", job_count=1, spec=spec, track=False)

        def check_assignment(tag, agent):
            op = run_with_tag(tag)
            wait(lambda: get_controller_agent(op) == agent)

        def check_no_agent(tag):
            op = run_with_tag(tag)
            wait(lambda: op.get_state() == "waiting_for_agent")
            time.sleep(1)
            assert get_controller_agent(op) is None

        check_assignment("foo", foo_agent)
        check_assignment("bar", bar_agent)
        check_assignment(None, default_agent)
        check_no_agent("baz")

        # foo -> foo
        # bar -> baz
        # default -> boo, booo
        foo_agent, baz_agent, boo_agent = foo_agent, bar_agent, default_agent
        with Restarter(self.Env, CONTROLLER_AGENTS_SERVICE):
            set("//sys/controller_agents/instances/{}/@tags".format(baz_agent), ["baz"])
            set(
                "//sys/controller_agents/instances/{}/@tags".format(default_agent),
                ["boo", "booo"],
            )

        wait_for_tags_loaded()
        assert get("//sys/controller_agents/instances/{}/orchid/controller_agent/tags".format(foo_agent)) == ["foo"]
        assert get("//sys/controller_agents/instances/{}/orchid/controller_agent/tags".format(baz_agent)) == ["baz"]
        assert get("//sys/controller_agents/instances/{}/orchid/controller_agent/tags".format(boo_agent)) == [
            "boo",
            "booo",
        ]

        check_assignment("foo", foo_agent)
        check_assignment("baz", baz_agent)
        check_assignment("boo", boo_agent)
        check_assignment("booo", boo_agent)
        check_no_agent(None)
        check_no_agent("bar")


##################################################################


class TestOperationControllerLimit(YTEnvSetup):
    NUM_MASTERS = 1
    NUM_NODES = 3
    NUM_SCHEDULERS = 1

    DELTA_CONTROLLER_AGENT_CONFIG = {
        "controller_agent": {
            "tagged_memory_statistics_update_period": 100,
            "operation_controller_memory_limit": 100
        }
    }

    @authors("gritukan")
    def test_operation_controller_memory_limit_exceeded(self):
        op = run_test_vanilla("sleep 1000", job_count=1)
        with raises_yt_error(ControllerMemoryLimitExceeded):
            op.track()
