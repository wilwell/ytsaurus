from ytbase import *

GetLog = FileDescr('get_log', ('aggregate', 'exec'))
Prepare = FileDescr('prepare', ('aggregate', 'exec', ))
DoRun = FileDescr('do_run', ('remote', 'exec'))
DoStop = FileDescr('do_stop', ('remote', 'exec'))
DoClean = FileDescr('do_clean', ('remote', 'exec'))
DoTest = FileDescr('do_test', ('remote', 'exec'))
Test = FileDescr('test', ('aggregate', 'exec'))

Files = [Config, Prepare, DoRun, Run, DoStop, Stop, Clean, DoClean, GetLog, Test, DoTest]

################################################################

shebang = '#!/bin/bash'
ulimit = 'ulimit -c unlimited'

cmd_run = 'start-stop-daemon -d ./ -b --exec %(work_dir)s/%(binary)s ' + \
            '--pidfile %(work_dir)s/pid -m -S -- %(params)s'
cmd_test = 'start-stop-daemon -d ./ -b -t --exec %(work_dir)s/%(binary)s ' + \
            '--pidfile %(work_dir)s/pid -m -S'
cmd_stop = 'start-stop-daemon --pidfile %(work_dir)s/pid -K'

cmd_ssh = 'ssh %s %s'
cmd_rsync = 'rsync --copy-links %s %s:%s'

def wrap_cmd(cmd, silent=False):
    res = ['cmd="timeout 10s %s"' % cmd]
    if silent:
        res.append('$cmd 2&>1 1>/dev/null')
    else:
        res.append('$cmd')
    res.append('''if [ $? -eq 124 ]; then
    echo "Command timed out: " $cmd
    exit
elif [ $? -ne 0 ]; then
    echo "Command failed: " $cmd
    exit
fi''')
    return '\n'.join(res)


class RemoteNode(Node):
    files = Files
    
    def remote_path(cls, filename):
        return os.path.join(cls.remote_dir, filename)
    
    @initmethod
    def init(cls):
        cls._init_path()
        cls.remote_dir = os.path.join(cls.base_dir, cls.path)
        cls.work_dir = cls.remote_dir
        
        for descr in cls.files:
            if 'remote' in descr.attrs:
                setattr(cls, '_'.join((descr.name, 'path')),
                        cls.remote_path(descr.filename))
            else:
                setattr(cls, '_'.join((descr.name, 'path')),
                        cls.local_path(descr.filename))
                        
    export_ld_path = Template('export LD_LIBRARY_PATH=%(remote_dir)s')
    
    def prepare(cls, fd):
        print >>fd, shebang
        print >>fd, wrap_cmd(cmd_ssh % (cls.host, "mkdir -p %s" % cls.remote_dir))
        print >>fd, wrap_cmd(cmd_rsync % (cls.bin_path, cls.host, cls.remote_dir))

        libs = getattr(cls, 'libs', None)
        if (libs):
            for lib in libs:
                cmd = cmd_rsync % (lib, cls.host, cls.remote_dir)
                print >>fd, wrap_cmd(cmd)
                
        
        for descr in cls.files:
            if 'remote' in descr.attrs:
                try:
                    cmd = cmd_rsync % (os.path.join(cls.local_path(descr.filename)), 
                        cls.host, cls.remote_dir)
                except:
                    print cls.__dict__
                    raise 'Zadnica'
                print >>fd, wrap_cmd(cmd) 

    run_tmpl = Template(cmd_run)
    def do_run(cls, fd):
        print >>fd, shebang
        print >>fd, ulimit
        print >>fd, cls.export_ld_path
        print >>fd, wrap_cmd(cls.run_tmpl)
    
    def run(cls, fd):
        print >>fd, shebang
        print >>fd, wrap_cmd(cmd_ssh % (cls.host, cls.do_run_path))
        
    stop_tmpl = Template(cmd_stop)
    def do_stop(cls, fd):
        print >>fd, shebang
        print >>fd, wrap_cmd(cls.stop_tmpl, True)

    test_tmpl = Template(cmd_test)
    def do_test(cls, fd):
        print >>fd, shebang
        print >>fd, 'cmd="%s"' % cls.test_tmpl
        print >>fd, '$cmd 2>&1 1>/dev/null'
        print >>fd, 'if [ $? -eq 0 ]; then'
        print >>fd, 'echo "Node is dead: %s, host: %s"' % (cls.work_dir, cls.host)
        print >>fd, 'fi' 
    
    def stop(cls, fd):
        print >>fd, shebang
        print >>fd, wrap_cmd(cmd_ssh % (cls.host, cls.do_stop_path))

    def test(cls, fd):
        print >>fd, shebang
        print >>fd, wrap_cmd(cmd_ssh % (cls.host, cls.do_test_path))
        
    def clean(cls, fd):
        print >>fd, shebang
        print >>fd, wrap_cmd(cmd_ssh % (cls.host, cls.do_clean_path))


class RemoteServer(RemoteNode, ServerNode):
    pass
                            
##################################################################

def configure(root):
    make_files(root)
    make_aggregate(root, lambda x:x + '&', 'wait')
    
    hosts = set()
    def append_hosts(node):
        for l in node.__leafs:
            append_hosts(l)
        if not node.__leafs:
            host = getattr(node, 'host', None)
            if host:
                hosts.add((host, node))
    append_hosts(root)
        
    with open(root.local_path('remove_all.' + SCRIPT_EXT), 'w') as fd:
        print >>fd, shebang
        for host, node in hosts:
            print >>fd, cmd_ssh % (host, 'rm -rf %s' % os.path.join(node.base_dir, root.path))
    make_executable(fd.name)
        
