#!/usr/bin/perl -w -T
#-----------------------------------------------------------------------------
#
# The malloc monitor daemon.
#
# Written by Ryan C. Gordon (icculus@icculus.org)
#
# Please see the file LICENSE in the source's root directory.
#
#-----------------------------------------------------------------------------

use strict;             # don't touch this line, nootch.
use warnings;           # don't touch this line, either.
use IO::Select;         # bleh.

my $version = '0.0.1';
my $protocol_version = 1;

#-----------------------------------------------------------------------------#
#             CONFIGURATION VARIABLES: Change to suit your needs...           #
#-----------------------------------------------------------------------------#

# The processes path is replaced with this string, for security reasons, and
#  to satisfy the requirements of Taint mode. Make this as simple as possible.
#  Currently, the only thing that uses the PATH environment var is the
#  "fortune" fakeuser, which can be safely removed.
my $safe_path = '';

# Turn the process into a daemon. This will handle creating/answering socket
#  connections, and forking off children to handle them. This flag can be
#  toggled via command line options (--daemonize, --no-daemonize, -d), but
#  this sets the default. Daemonizing tends to speed up processing (since the
#  script stays loaded/compiled), but may cause problems on systems that
#  don't have a functional fork() or IO::Socket::INET package. If you don't
#  daemonize, this program reads requests from stdin and writes results to
#  stdout, which makes it suitable for command line use or execution from
#  inetd and equivalents.
my $daemonize = 0;
my $background = 1;
my $dropprivs = 1;

# Run for one connection and exit, no fork. Good for debugging and profiling.
my $onerun = 0;

# This is only used when daemonized. Specify the port on which to listen for
#  incoming connections.
my $server_port = 22222;

# Set this to immediately drop priveledges by setting uid and gid to these
#  values. Set to undef to not attempt to drop privs.
my $wanted_uid = undef;
my $wanted_gid = undef;
#my $wanted_uid = 1056;  # (This is the uid of "finger" ON _MY_ SYSTEM.)
#my $wanted_gid = 971;   # (This is the gid of "iccfinger" ON _MY_ SYSTEM.)

# This is only used when daemonized. Specify the maximum number of
#  requests to service at once. A separate child process is fork()ed off for
#  each request, and if there are more requests then this value, the extra
#  clients will be made to wait until some of the current requests are
#  serviced. 5 to 10 is usually a good number. Set it higher if you get a
#  massive amount of finger requests simultaneously.
my $max_connects = 10;

# This is how long, in seconds, before an idle connection will be summarily
#  dropped. This prevents abuse from people hogging a connection without
#  actually sending a request, without this, enough connections like this
#  will block legitimate ones. At worst, they can only block for this long
#  before being booted and thus freeing their connection slot for the next
#  guy in line. Setting this to undef lets people sit forever, but removes
#  reliance on the IO::Select package. Note that this timeout is how long
#  the user has to complete the read_request() function, so don't set it so
#  low that legitimate lag can kill them. The default is usually safe.
my $read_timeout = 30;

# Set this to non-zero to log all requests via the standard Unix
#  syslog facility (requires Sys::Syslog qw(:DEFAULT setlogsock) ...)
my $use_syslog = 1;

# This is the maximum size, in bytes, that a finger request can be. This is
#  to prevent malicious finger clients from trying to fill all of system
#  memory.
my $max_request_size = 512;

# You can screw up your output with this, if you like.
my $debug = 0;


#-----------------------------------------------------------------------------#
#     The rest is probably okay without you laying yer dirty mits on it.      #
#-----------------------------------------------------------------------------#

sub syslogwarn {
    my $w = shift;
    #syslog('info', "$w\n") if ($use_syslog);
    print STDERR "$w\n" if ($use_syslog);
}

sub syslog_and_die {
    my $err = shift;
    syslogwarn($err);
    die("$err\n");
}

sub debug {
    my $str = shift;
    syslogwarn($str) if ($debug);
}

sub read_block {
    my $maxchars = shift;
    my $terminator = shift;
    my $retval = '';
    my $count = 0;
    my $s = undef;
    my $elapsed = undef;
    my $starttime = undef;

    if (defined $read_timeout) {
        $s = new IO::Select();
        $s->add(fileno(STDIN));
        $starttime = time();
        $elapsed = 0;
    }

    while (1) {
        if (defined $read_timeout) {
            my $ready = scalar($s->can_read($read_timeout - $elapsed));
            return undef if (not $ready);
            $elapsed = (time() - $starttime);
        }

        my $ch;
        my $rc = sysread(STDIN, $ch, 1);
        return undef if ($rc != 1);
        return $retval if ((defined $terminator) and ($ch eq $terminator));
        $retval .= $ch;
        $count++;
        return $retval if ((defined $maxchars) and ($count >= $maxchars));
    }

    return(undef);  # shouldn't ever hit this.
}


# assume incoming binary data is littleendian by default.
my $unpackui16 = 'v';
my $unpackui32 = 'V';
my $unpackui64 = 'Q';  # !!! FIXME!

sub read_ui8_timeout {
    # respects predefined timeout, but is too slow for continual use.
    my $byte = read_block(1);
    return undef if not defined $byte;
    return(scalar(unpack('C', $byte)));
}

sub read_ui8 {
    my $byte;
    return undef if (sysread(STDIN, $byte, 1) != 1);
    return(scalar(unpack('C', $byte)));
}

sub read_ui16 {
    my $bytes;
    return undef if (sysread(STDIN, $bytes, 2) != 2);
    return(scalar(unpack($unpackui16, $bytes)));
}

sub read_ui32 {
    my $bytes;
    return undef if (sysread(STDIN, $bytes, 4) != 4);
    return(scalar(unpack($unpackui32, $bytes)));
}

sub read_ui64 {
    my $bytes;
    return undef if (sysread(STDIN, $bytes, 8) != 8);
    return(scalar(unpack($unpackui64, $bytes)));
}

sub go_to_background {
    use POSIX 'setsid';
    chdir('/') or syslog_and_die("Can't chdir to '/': $!");
    open STDIN,'/dev/null' or syslog_and_die("Can't read '/dev/null': $!");
    open STDOUT,'>/dev/null' or syslog_and_die("Can't write '/dev/null': $!");
    defined(my $pid=fork) or syslog_and_die("Can't fork: $!");
    exit if $pid;
    setsid or syslog_and_die("Can't start new session: $!");
    open STDERR,'>&STDOUT' or syslog_and_die("Can't duplicate stdout: $!");
    syslogwarn("Daemon process is now detached");
}


sub drop_privileges {
    delete @ENV{qw(IFS CDPATH ENV BASH_ENV)};
    $ENV{'PATH'} = $safe_path;
    $) = $wanted_gid if (defined $wanted_gid);
    $> = $wanted_uid if (defined $wanted_uid);
}


sub signal_catcher {
    my $sig = shift;
    syslogwarn("Got signal $sig. Shutting down.");
    exit 0;
}


my %allowed_ips = ();

sub allowed_ip {
    my $ip = shift;

    # No limits specified? Welcome the world.
    return 1 if (scalar(keys(%allowed_ips)) == 0);

    # Is this IP specified?
    return 1 if (exists $allowed_ips{$ip});

    # Check subnets...
    $ip =~ s/\A(\d+\.\d+\.\d+\.)\d+\Z/$1/ or return 0;
    return 1 if (exists $allowed_ips{$ip});
    $ip =~ s/\A(\d+\.\d+\.)\d+\.\Z/$1/ or return 0;
    return 1 if (exists $allowed_ips{$ip});
    $ip =~ s/\A(\d+\.)\d+.\Z/$1/ or return 0;
    return 1 if (exists $allowed_ips{$ip});

    # Not welcome.
    return 0;
}


my @kids;
use POSIX ":sys_wait_h";
sub reap_kids {
    my $i = 0;
    my $x = scalar(@kids);
    while ($i < scalar(@kids)) {
        my $rc = waitpid($kids[$i], &WNOHANG);
        if ($rc != 0) {  # reaped a zombie.
            splice(@kids, $i, 1); # take it out of the array.
        } else {  # still alive, try next one.
            $i++;
        }
    }

    $SIG{CHLD} = \&reap_kids;  # make sure this works on crappy SysV systems.
}


my $bigendian = 0;
my $sizeofptr = 0;
my $monitor_client_fname = '';
my $monitor_client_pid = 0;
my $monitor_client_id = '';

sub read_handshake {
    my $hello = read_block(16, "\0");
    return 0 if (not defined $hello) or ($hello ne 'Malloc Monitor!');

    my $prot = read_ui8_timeout();
    return 0 if (not defined $prot);
    if ($prot != $protocol_version) {
        syslogwarn("Protocol version $prot, wanted $protocol_version");
        return 0;
    }

    $bigendian = read_ui8_timeout();
    return 0 if (not defined $bigendian);
    return 0 if (($bigendian != 0) and ($bigendian != 1));
    if ($bigendian == 0) {
        $unpackui16 = 'v';
        $unpackui32 = 'V';
        $unpackui64 = 'Q';  # !!! FIXME!
    } else {
        $unpackui16 = 'n';
        $unpackui32 = 'N';
        $unpackui64 = 'Q';  # !!! FIXME!
    }
    $sizeofptr = read_ui8_timeout();  # only handles 32 and 64-bit right now.
    return 0 if (($sizeofptr != 4) and ($sizeofptr != 8));
    # !!! TODO my $passwd = read_block(64, "\0");
    $monitor_client_id = read_block(64, "\0");
    return 0 if (not defined $monitor_client_id);
    return 0 if (not $monitor_client_id =~ /\A[a-zA-Z0-9]+\Z/);
    $monitor_client_fname = read_block(1024, "\0");
    return 0 if (not defined $monitor_client_fname);
    $monitor_client_pid = read_ui32();
    return 0 if (not defined $monitor_client_pid);
    return 1;
}


sub read_callstack {
    my $count = read_ui32();
    syslogwarn("unexpected connection drop") if not defined $count;

    while ($count) {
        my $frame = read_ptr();
        syslogwarn("unexpected connection drop") if not defined $frame;
        $count--;
    }
    return 1;
}

sub read_native_word {
    my $datatype = shift;
    my $val = ($sizeofptr == 4) ? read_ui32() : read_ui64();
    if (not defined $val) {
        syslogwarn("unexpected connection drop");
    } else {
        debug("   - $datatype : $val");
    }
    return $val;
}

sub read_sizet {
    #return(read_native_word('size_t'));
    my $val = (($sizeofptr == 4) ? read_ui32() : read_ui64());
    syslogwarn('unexpected connection drop'), return 0 if not defined $val;
    return $val;
}

sub read_ptr {
    #return(read_native_word('pointer'));
    my $val = (($sizeofptr == 4) ? read_ui32() : read_ui64());
    syslogwarn('unexpected connection drop'), return 0 if not defined $val;
    return $val;
}

sub read_ticks {
    #return(read_native_word('ticks'));
    my $val = read_ui32();
    syslogwarn('unexpected connection drop'), return 0 if not defined $val;
    return $val;
}

use constant MONITOR_OP_NOOP     => 0;
use constant MONITOR_OP_GOODBYE  => 1;
use constant MONITOR_OP_MALLOC   => 2;
use constant MONITOR_OP_REALLOC  => 3;
use constant MONITOR_OP_MEMALIGN => 4;
use constant MONITOR_OP_FREE     => 5;

sub do_noop_operation {
    debug(' + NOOP operation.');
    return 1;
}

sub do_goodbye_operation {
    debug(' + GOODBYE operation.');
    return 0;
}

sub do_malloc_operation {
    debug(' + MALLOC operation.');
    my $t = read_ticks(); return 0 if (not defined $t);
    my $s = read_sizet(); return 0 if (not defined $s);
    my $rc = read_ptr(); return 0 if (not defined $rc);
    my $c = read_callstack(); return 0 if (not defined $c);
    # !!! FIXME: do something.
    return 1;
}

sub do_realloc_operation {
    debug(' + REALLOC operation.');
    my $t = read_ticks(); return 0 if (not defined $t);
    my $p = read_ptr(); return 0 if (not defined $p);
    my $s = read_sizet(); return 0 if (not defined $s);
    my $rc = read_ptr(); return 0 if (not defined $rc);
    my $c = read_callstack(); return 0 if (not defined $c);
    # !!! FIXME: do something.
    return 1;
}

sub do_memalign_operation {
    debug(' + MEMALIGN operation.');
    my $t = read_ticks(); return 0 if (not defined $t);
    my $b = read_sizet(); return 0 if (not defined $b);
    my $s = read_sizet(); return 0 if (not defined $s);
    my $rc = read_ptr(); return 0 if (not defined $rc);
    my $c = read_callstack(); return 0 if (not defined $c);
    # !!! FIXME: do something.
    return 1;
}

sub do_free_operation {
    debug(' + FREE operation.');
    my $t = read_ticks(); return 0 if (not defined $t);
    my $p = read_ptr(); return 0 if (not defined $p);
    my $c = read_callstack(); return 0 if (not defined $c);
    # !!! FIXME: do something.
    return 1;
}

sub do_operation {
    my $op = read_ui8();
    return 0 if not defined $op;

    return do_noop_operation() if ($op == MONITOR_OP_NOOP);
    return do_goodbye_operation() if ($op == MONITOR_OP_GOODBYE);
    return do_malloc_operation() if ($op == MONITOR_OP_MALLOC);
    return do_realloc_operation() if ($op == MONITOR_OP_REALLOC);
    return do_memalign_operation() if ($op == MONITOR_OP_MEMALIGN);
    return do_free_operation() if ($op == MONITOR_OP_FREE);

    debug("Unknown operation $op");
    return 0;
}


# Called when connection is made. Read and write to stdin/stdout to talk over
#  socket, use syslogwarn() for sysadmin info. Return value for child process
#  to use when terminating.
sub server_mainline {
    syslogwarn("bogus/incomplete handshake"), return 0 if not read_handshake();

    debug(' + handshake complete:');
    debug("   - protocol version == $protocol_version");
    debug("   - byteorder == " . (($bigendian) ? "bigendian":"littleendian"));
    debug("   - sizeofptr == $sizeofptr");
    debug("   - clientid == '$monitor_client_id'");

    # no longer care if client is quiet for long amounts of time.
    $read_timeout = undef;

    1 while do_operation();

    syslogwarn("Connection terminated");
    return(0);
}


sub daemon_upkeep {
    # no-op.
}


sub parse_cmdline {
    foreach (@_) {
        chomp;
        s/\A\s*//;
        s/\s*\Z//;
        next if ($_ eq '');
        if (s/\A--config=(.*?)\Z/$1/) {
            open(CFG, '<', $_) or die("Can't open $_: $!\n");
            my @vals = <CFG>;
            close(CFG);
            parse_cmdline(@vals);
            next;
        }
        $daemonize = 1, next if $_ eq '--daemonize';
        $daemonize = 1, next if $_ eq '-d';
        $daemonize = 0, next if $_ eq '--no-daemonize';
        $debug = 1, next if $_ eq '--debug';
        $debug = 0, next if $_ eq '--no-debug';
        $use_syslog = 1, next if $_ eq '--syslog';
        $use_syslog = 0, next if $_ eq '--no-syslog';
        $background = 0, next if $_ eq '--no-background';
        $background = 1, next if $_ eq '--background';
        $dropprivs = 0, next if $_ eq '--no-dropprivs';
        $dropprivs = 1, next if $_ eq '--dropprivs';
        $onerun = 0, next if $_ eq '--no-onerun';
        $onerun = 1, next if $_ eq '--onerun';
        $allowed_ips{$_} = 1, next if s/\A--allow-ip=(.*?)\Z/$1/;
        $server_port = $_, next if s/\A--port=(.*?)\Z/$1/;
        die("Unknown command line \"$_\".\n");
    }
}

# Mainline.
parse_cmdline(@ARGV);

#if ($use_syslog) {
#    use Sys::Syslog qw(:DEFAULT setlogsock);
#    setlogsock('unix');
#    openlog('MallocMonitor', 'pid', 'user') or die("Couldn't open syslog: $!\n");
#}

my $retval = 0;
if (not $daemonize) {
    drop_privileges();
    $retval = server_mainline();
    exit $retval;
}

# The daemon.

syslogwarn("Malloc Monitor daemon $version starting up...");

go_to_background() if $background;

# reap zombies from client forks...
$SIG{CHLD} = \&reap_kids;
$SIG{TERM} = \&signal_catcher;
$SIG{INT} = \&signal_catcher;

# !!! FIXME: bind to a specific interface!
use IO::Socket::INET;
my $listensock = IO::Socket::INET->new(LocalPort => $server_port,
                                       Type => SOCK_STREAM,
                                       ReuseAddr => 1,
                                       Listen => $max_connects);

syslog_and_die("couldn't create listen socket: $!") if (not $listensock);

my $selection = new IO::Select( $listensock );
drop_privileges() if ($dropprivs);

syslogwarn("Now accepting connections (max $max_connects" .
           " simultaneous on port $server_port).");

while (1)
{
    # prevent connection floods.
    daemon_upkeep(), sleep(1) while (scalar(@kids) >= $max_connects);

    # if timed out, do upkeep and try again.
    daemon_upkeep() while not $selection->can_read(10);

    # we've got a connection!
    my $client = $listensock->accept();
    if (not $client) {
        syslogwarn("accept() failed: $!");
        next;
    }

    my $ip = $client->peerhost();

    # limit access to specific IPs...
    if (not allowed_ip($ip)) {
        syslogwarn("Unallowed IP $ip attempted connection.");
        close($client);
        next;
    }

    syslogwarn("connection from $ip");

    if (not $onerun) {
        my $kidpid = fork();
        if (not defined $kidpid) {
            syslogwarn("fork() failed: $!");
            close($client);
            next;
        }

        if ($kidpid) {  # this is the parent process.
            close($client);  # parent has no use for client socket.
            push @kids, $kidpid;
            next;
        }
    }

    # either the child process, or it's a onerun.
    $ENV{'TCPREMOTEIP'} = $ip;
    close($listensock);   # child has no use for listen socket.
    local *FH = $client;
    open(STDIN,  '<&FH') or syslog_and_die("no STDIN reassign: $!");
    #open(STDERR, '>&FH') or syslog_and_die("no STDERR reassign: $!");
    open(STDOUT, '>&FH') or syslog_and_die("no STDOUT reassign: $!");
    my $retval = server_mainline();
    close($client);
    close(FH);
    exit $retval;  # kill child.
}

close($listensock);  # shouldn't ever hit this.
exit $retval;

# end of malloc_monitor_daemon.pl ...

