#!/usr/bin/env python

from yt.common import update
import yt.yson as yson

from yt.fennel.new_fennel import LogBroker, monitor, push_to_logbroker
import yt.wrapper as yt

import argparse

def make_yt_client(args):
    config = {"read_retries": {"allow_multiple_ranges": True}}
    return yt.YtClient(args.yt_proxy, config=update(config, yson.loads(args.yt_config)))

def make_logbroker_client(args):
    if args.logbroker_source_id is None:
        args.logbroker_source_id = args.yt_proxy.split(".")[0]

    return LogBroker(
        args.logbroker_url,
        args.logbroker_log_type,
        args.logbroker_service_id,
        args.logbroker_source_id,
        args.logbroker_chunk_size)


def parse_monitor(args):
    client = make_yt_client(args)
    monitor(client, args.threshold)

def add_monitor_parser(subparsers, parent_parser):
    parser = subparsers.add_parser("monitor", help="Juggler compatible monitor of event_log state", parents=[parent_parser])
    parser.add_argument("--threshold", type=int, help="Maximum value of allowed lag in minutes", default=60)
    parser.set_defaults(func=parse_monitor)

def parse_push_to_logbroker(args):
    yt_client = make_yt_client(args)
    logbroker = make_logbroker_client(args)

    with yt_client.Transaction():
        push_to_logbroker(yt_client,
                          logbroker,
                          daemon=args.daemon,
                          lock_path=args.lock_path,
                          sentry_endpoint=args.sentry_endpoint,
                          table_path=args.table_path,
                          session_count=args.session_count,
                          range_row_count=args.range_row_count,
                          max_range_count=args.max_range_count)

def add_push_to_logbroker_parser(subparsers, parent_parser):
    parser = subparsers.add_parser("push-to-logbroker",
                                   help="Tail table and push data to logbroker",
                                   parents=[parent_parser])
    parser.add_argument("--table-path",
                        help="Path to table that should be pushed to logbroker. "
                             "By default it is //sys/scheduler/event_log",
                        default="//sys/scheduler/event_log")
    parser.add_argument("--session-count", type=int,
                        help="Number of parallel sessions to tail and push", default=1)
    parser.add_argument("--range-row-count", type=int,
                        help="Number of rows per one range", default=10000)
    parser.add_argument("--max-range-count", type=int,
                        help="Number of ranges per one task", default=10)
    parser.add_argument("--logbroker-url", required=True,
                        help="Url of logbroker")
    parser.add_argument("--logbroker-chunk-size", type=int,
                        help="Size of chunk to split data in /rt/store command", default=1024 * 1024)
    parser.add_argument("--logbroker-source-id",
                        help="Source id for log broker. If used more than one session then "
                             "'_NUM' will be added to session id. By default session name "
                             "generated from cluster name")
    parser.add_argument("--logbroker-log-type", help="Name of log type that used by logbroker", default="yt-scheduler-log")
    parser.add_argument("--logbroker-service-id", help="Name of service", default="yt")
    parser.add_argument("--sentry-endpoint", help="sentry endpoint")
    parser.add_argument("--lock-path", help="Path in cypress to avoid running more than one instance of fennel")
    daemon_parser = parser.add_mutually_exclusive_group(required=False)
    daemon_parser.add_argument("--daemon", dest="daemon", action="store_true")
    daemon_parser.add_argument("--non-daemon", dest="daemon", action="store_false")

    parser.set_defaults(func=parse_push_to_logbroker)


def main():
    parser = argparse.ArgumentParser(description="Fennel: tool for processing YT shceduler event log")

    parent_parser = argparse.ArgumentParser(add_help=False)
    parent_parser.add_argument("--yt-proxy", required=True, help="yt proxy")
    parent_parser.add_argument("--yt-config", default={}, help="yt config")

    subparsers = parser.add_subparsers(help="Command: monitor or push-to-logbroker", metavar="command")
    add_monitor_parser(subparsers, parent_parser)
    add_push_to_logbroker_parser(subparsers, parent_parser)

    args, other = parser.parse_known_args()
    args.func(args)

    # Attributes on table:
    # processed_row_count - number of rows in current table pushed to logbroker.
    # archived_row_count - number of archived rows.
    # last_saved_ts - timestamp of last record pushed to logbroker.
    # table_start_row_index - number of processed rows before the beginning of table.
    # Should be correctly recalculated by rotation script.
    #
    # Each row in table should have 'timestamp' column

if __name__ == "__main__":
    main()
