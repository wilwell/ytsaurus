# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(23.0.0)

LICENSE(MIT)

PEERDIR(
    contrib/python/packaging
)

NO_LINT()

NO_CHECK_IMPORTS(
    gunicorn.app.pasterapp
    gunicorn.workers.geventlet
    gunicorn.workers.ggevent
    gunicorn.workers.gtornado
)

PY_SRCS(
    TOP_LEVEL
    gunicorn/__init__.py
    gunicorn/__main__.py
    gunicorn/app/__init__.py
    gunicorn/app/base.py
    gunicorn/app/pasterapp.py
    gunicorn/app/wsgiapp.py
    gunicorn/arbiter.py
    gunicorn/config.py
    gunicorn/debug.py
    gunicorn/errors.py
    gunicorn/glogging.py
    gunicorn/http/__init__.py
    gunicorn/http/body.py
    gunicorn/http/errors.py
    gunicorn/http/message.py
    gunicorn/http/parser.py
    gunicorn/http/unreader.py
    gunicorn/http/wsgi.py
    gunicorn/instrument/__init__.py
    gunicorn/instrument/statsd.py
    gunicorn/pidfile.py
    gunicorn/reloader.py
    gunicorn/sock.py
    gunicorn/systemd.py
    gunicorn/util.py
    gunicorn/workers/__init__.py
    gunicorn/workers/base.py
    gunicorn/workers/base_async.py
    gunicorn/workers/geventlet.py
    gunicorn/workers/ggevent.py
    gunicorn/workers/gthread.py
    gunicorn/workers/gtornado.py
    gunicorn/workers/sync.py
    gunicorn/workers/workertmp.py
)

RESOURCE_FILES(
    PREFIX contrib/python/gunicorn/py3/
    .dist-info/METADATA
    .dist-info/entry_points.txt
    .dist-info/top_level.txt
)

END()
