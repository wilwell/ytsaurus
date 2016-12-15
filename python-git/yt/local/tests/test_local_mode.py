from __future__ import print_function

from yt.local import start, stop, delete
import yt.local as yt_local
from yt.wrapper.client import Yt
from yt.common import remove_file, is_process_alive
from yt.wrapper.common import generate_uuid
import yt.subprocess_wrapper as subprocess

from yt.packages.six.moves import map as imap, xrange
from yt.packages.six import iteritems

import yt.wrapper as yt

import yt.yson as yson
import yt.json as json

import os
import sys
import pytest
import tempfile
import signal
import contextlib
import time

TESTS_LOCATION = os.path.dirname(os.path.abspath(__file__))
TESTS_SANDBOX = os.environ.get("TESTS_SANDBOX", os.path.join(TESTS_LOCATION, "sandbox"))
LOCAL_MODE_TESTS_SANDBOX = os.path.join(TESTS_SANDBOX, "TestLocalMode")
YT_LOCAL_BINARY = os.path.join(os.path.dirname(TESTS_LOCATION), "bin", "yt_local")

def _get_instance_path(instance_id):
    return os.path.join(LOCAL_MODE_TESTS_SANDBOX, instance_id)

def _read_pids_file(instance_id):
    pids_filename = os.path.join(_get_instance_path(instance_id), "pids.txt")
    if not os.path.exists(pids_filename):
        return []
    with open(pids_filename) as f:
        return list(imap(int, f))

def _is_exists(environment):
    return os.path.exists(_get_instance_path(environment.id))

def _wait_instance_to_become_ready(process, instance_id):
    special_file = os.path.join(_get_instance_path(instance_id), "started")

    attempt_count = 10
    for _ in xrange(attempt_count):
        print("Waiting instance", instance_id, "to become ready...")
        if os.path.exists(special_file):
            return

        if process.poll() is not None:
            stderr = process.stderr.read()
            raise yt.YtError("Local YT instance process exited with error code {0}: {1}"
                             .format(process.returncode, stderr))

        time.sleep(1.0)

    raise yt.YtError("Local YT is not started")

@contextlib.contextmanager
def local_yt(*args, **kwargs):
    environment = None
    try:
        environment = start(*args, **kwargs)
        yield environment
    finally:
        if environment is not None:
            stop(environment.id)

class YtLocalBinary(object):
    def __init__(self, root_path, port_locks_path):
        self.root_path = root_path
        self.port_locks_path = port_locks_path

    def _prepare_binary_command_and_env(self, *args, **kwargs):
        command = [sys.executable, YT_LOCAL_BINARY] + list(args)

        for key, value in iteritems(kwargs):
            key = key.replace("_", "-")
            if value is True:
                command.extend(["--" + key])
            else:
                command.extend(["--" + key, str(value)])

        env = {
            "YT_LOCAL_ROOT_PATH": self.root_path,
            "YT_LOCAL_PORT_LOCKS_PATH": self.port_locks_path,
            "PYTHONPATH": os.environ["PYTHONPATH"],
            "PATH": os.environ["PATH"],
            "YT_LOCAL_USE_PROXY_FROM_SOURCE": "1"
        }
        return command, env

    def __call__(self, *args, **kwargs):
        command, env = self._prepare_binary_command_and_env(*args, **kwargs)
        return subprocess.check_output(command, env=env).strip()

    def run_async(self, *args, **kwargs):
        command, env = self._prepare_binary_command_and_env(*args, **kwargs)
        return subprocess.Popen(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

class TestLocalMode(object):
    @classmethod
    def setup_class(cls):
        cls.old_yt_local_root_path = os.environ.get("YT_LOCAL_ROOT_PATH", None)
        cls.old_yt_local_use_proxy_from_source = \
                os.environ.get("YT_LOCAL_USE_PROXY_FROM_SOURCE", None)
        os.environ["YT_LOCAL_ROOT_PATH"] = LOCAL_MODE_TESTS_SANDBOX
        os.environ["YT_LOCAL_USE_PROXY_FROM_SOURCE"] = '1'
        # Add ports_lock_path argument to YTEnvironment for parallel testing.
        os.environ["YT_LOCAL_PORT_LOCKS_PATH"] = os.path.join(TESTS_SANDBOX, "ports")
        cls.yt_local = YtLocalBinary(os.environ["YT_LOCAL_ROOT_PATH"],
                                     os.environ["YT_LOCAL_PORT_LOCKS_PATH"])

    @classmethod
    def teardown_class(cls):
        if cls.old_yt_local_root_path is not None:
            os.environ["YT_LOCAL_ROOT_PATH"] = cls.old_yt_local_root_path
        if cls.old_yt_local_use_proxy_from_source is not None:
            os.environ["YT_LOCAL_USE_PROXY_FROM_SOURCE"] = \
                    cls.old_yt_local_use_proxy_from_source
        del os.environ["YT_LOCAL_PORT_LOCKS_PATH"]

    def test_commands_sanity(self):
        with local_yt() as environment:
            pids = _read_pids_file(environment.id)
            assert len(pids) == 4
            # Should not delete running instance
            with pytest.raises(yt.YtError):
                delete(environment.id)
        # Should not stop already stopped instance
        with pytest.raises(yt.YtError):
            stop(environment.id)
        assert not os.path.exists(environment.pids_filename)
        delete(environment.id)
        assert not _is_exists(environment)

    def test_start(self):
        with pytest.raises(yt.YtError):
            start(master_count=0)

        with local_yt(master_count=3, node_count=0, scheduler_count=0,
                      enable_debug_logging=True) as environment:
            assert len(_read_pids_file(environment.id)) == 4  # + proxy
            assert len(environment.configs["master"]) == 3

        with local_yt(node_count=5, scheduler_count=2, start_proxy=False) as environment:
            assert len(environment.configs["node"]) == 5
            assert len(environment.configs["scheduler"]) == 2
            assert len(environment.configs["master"]) == 1
            assert len(_read_pids_file(environment.id)) == 8
            with pytest.raises(yt.YtError):
                environment.get_proxy_address()

        with local_yt(node_count=1) as environment:
            assert len(_read_pids_file(environment.id)) == 4  # + proxy

        with local_yt(node_count=0, scheduler_count=0, start_proxy=False) as environment:
            assert len(_read_pids_file(environment.id)) == 1

    def test_use_local_yt(self):
        with local_yt() as environment:
            proxy_port = environment.get_proxy_address().rsplit(":", 1)[1]
            client = Yt(proxy="localhost:{0}".format(proxy_port))
            client.config["tabular_data_format"] = yt.format.DsvFormat()
            client.mkdir("//test")

            client.set("//test/node", "abc")
            assert client.get("//test/node") == "abc"
            assert client.list("//test") == ["node"]

            client.remove("//test/node")
            assert not client.exists("//test/node")

            client.mkdir("//test/folder")
            assert client.get_type("//test/folder") == "map_node"

            table = "//test/table"
            client.create("table", table)
            client.write_table(table, [{"a": "b"}])
            assert [{"a": "b"}] == list(client.read_table(table))

            assert set(client.search("//test")) == set(["//test", "//test/folder", table])

    def test_use_context_manager(self):
        with yt_local.LocalYt() as client:
            client.config["tabular_data_format"] = yt.format.DsvFormat()
            client.mkdir("//test")

            client.set("//test/node", "abc")
            assert client.get("//test/node") == "abc"
            assert client.list("//test") == ["node"]

            client.remove("//test/node")
            assert not client.exists("//test/node")

            client.mkdir("//test/folder")
            assert client.get_type("//test/folder") == "map_node"

            table = "//test/table"
            client.create("table", table)
            client.write_table(table, [{"a": "b"}])
            assert [{"a": "b"}] == list(client.read_table(table))

            assert set(client.search("//test")) == set(["//test", "//test/folder", table])

    def test_local_cypress_synchronization(self):
        local_cypress_path = os.path.join(TESTS_LOCATION, "local_cypress_tree")
        with local_yt(local_cypress_dir=local_cypress_path) as environment:
            proxy_port = environment.get_proxy_address().rsplit(":", 1)[1]
            client = Yt(proxy="localhost:{0}".format(proxy_port))
            assert list(client.read_table("//table")) == [{"x": "1", "y": "1"}]
            assert client.get_type("//subdir") == "map_node"
            assert client.get_attribute("//table", "myattr") == 4
            assert client.get_attribute("//subdir", "other_attr") == 42
            assert client.get_attribute("/", "root_attr") == "ok"

    def test_preserve_state(self):
        with local_yt() as environment:
            client = environment.create_client()
            client.write_table("//home/my_table", [{"x": 1, "y": 2, "z": 3}])

        with local_yt(id=environment.id) as environment:
            client = environment.create_client()
            assert list(client.read_table("//home/my_table")) == [{"x": 1, "y": 2, "z": 3}]

    def test_configs_patches(self):
        patch = {"test_key": "test_value"}
        try:
            with tempfile.NamedTemporaryFile(dir=TESTS_SANDBOX, delete=False) as yson_file:
                yson.dump(patch, yson_file)
            with tempfile.NamedTemporaryFile(mode="w", dir=TESTS_SANDBOX, delete=False) as json_file:
                json.dump(patch, json_file)

            with local_yt(master_config=yson_file.name,
                          node_config=yson_file.name,
                          scheduler_config=yson_file.name,
                          proxy_config=json_file.name) as environment:
                for service in ["master", "node", "scheduler", "proxy"]:
                    if isinstance(environment.configs[service], list):
                        for config in environment.configs[service]:
                            assert config["test_key"] == "test_value"
                    else:  # Proxy config
                        assert environment.configs[service]["test_key"] == "test_value"
        finally:
            remove_file(yson_file.name, force=True)
            remove_file(json_file.name, force=True)

    def test_yt_local_binary(self):
        env_id = self.yt_local("start", fqdn="localhost")
        try:
            client = Yt(proxy=self.yt_local("get_proxy", env_id))
            assert "sys" in client.list("/")
        finally:
            self.yt_local("stop", env_id)

        env_id = self.yt_local("start", fqdn="localhost", master_count=3, node_count=5, scheduler_count=2)
        try:
            client = Yt(proxy=self.yt_local("get_proxy", env_id))
            assert len(client.list("//sys/nodes")) == 5
            assert len(client.list("//sys/scheduler/instances")) == 2
            assert len(client.list("//sys/primary_masters")) == 3
        finally:
            self.yt_local("stop", env_id, "--delete")

        patch = {"exec_agent": {"job_controller": {"resource_limits": {"user_slots": 100}}}}
        try:
            with tempfile.NamedTemporaryFile(dir=TESTS_SANDBOX, delete=False) as node_config:
                yson.dump(patch, node_config)
            with tempfile.NamedTemporaryFile(dir=TESTS_SANDBOX, delete=False) as config:
                yson.dump({"yt_local_test_key": "yt_local_test_value"}, config)

            env_id = self.yt_local(
                "start",
                fqdn="localhost",
                node_count=1,
                node_config=node_config.name,
                scheduler_count=1,
                scheduler_config=config.name,
                master_count=1,
                master_config=config.name)

            try:
                client = Yt(proxy=self.yt_local("get_proxy", env_id))
                node_address = client.list("//sys/nodes")[0]
                assert client.get("//sys/nodes/{0}/@resource_limits/user_slots".format(node_address)) == 100
                for subpath in ["primary_masters", "scheduler/instances"]:
                    address = client.list("//sys/{0}".format(subpath))[0]
                    assert client.get("//sys/{0}/{1}/orchid/config/yt_local_test_key"
                                      .format(subpath, address)) == "yt_local_test_value"
            finally:
                self.yt_local("stop", env_id)
        finally:
            remove_file(node_config.name, force=True)
            remove_file(config.name, force=True)

    def test_tablet_cell_initialization(self):
        with local_yt(wait_tablet_cell_initialization=True) as environment:
            client = environment.create_client()
            tablet_cells = client.list("//sys/tablet_cells")
            assert len(tablet_cells) == 1
            assert client.get("//sys/tablet_cells/{0}/@health".format(tablet_cells[0])) == "good"

    def test_all_processes_are_killed(self):
        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGKILL):
            env_id = generate_uuid()
            process = self.yt_local.run_async("start", sync=True, id=env_id)
            _wait_instance_to_become_ready(process, env_id)

            pids = _read_pids_file(env_id)
            assert all(is_process_alive(pid) for pid in pids)
            process.send_signal(sig)
            time.sleep(5.0)
            assert all(not is_process_alive(pid) for pid in pids)
