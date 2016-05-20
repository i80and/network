#!/usr/bin/perl -T
use strict;
use warnings;
use v5.18;

use POSIX;
use IPC::Open3;
use Carp;
use File::Temp;
use IO::KQueue;
use IO::Socket::UNIX;
use JSON::PP;
use Fcntl;

local $ENV{'PATH'} = '/bin:/sbin:/usr/bin:/usr/sbin';
my $SOCK_PATH = "/var/run/network.sock";

sub run {
    my ($cmd) = @_;
    my $output = `$cmd`;
    my $status = $? >> 8;
    if($status) { croak "Failed: '$cmd' with exit code $status"; }
    return $output;
}

sub check_iface {
    my ($iface) = @_;
    my ($checked) = $iface =~ m/^([a-z]+[0-9]*)$/ig
        or die "Invalid interface: '$iface'";
    return $checked;
}

sub list_ifaces {
    my %pseudo = map {($_, undef)} split(' ', run('/sbin/ifconfig -C'));

    my @lines = split('\n', run('/sbin/ifconfig'));
    my %ifaces = ();
    my $cur_iface;
    foreach my $line (@lines) {
        my @terms = $line =~ /^([a-z]+[0-9]*): flags=[0-9]+<(\S*)> mtu ([0-9]+)/;
        if(@terms) {
            my ($name, $flags, $mtu) = @terms;
            my %entry = ();
            @entry{'flags'} = [split(',', $flags)];
            $entry{'mtu'} = int($mtu);
            $ifaces{$name} = \%entry;

            $cur_iface = $name;
            next;
        }

        if(!defined $cur_iface) { next; }
        @terms = $line =~ /^\s+(\w+):? (.+)/;
        if($#terms < 1) { next; }
        $ifaces{$cur_iface}{$terms[0]} = $terms[1];
    }

    while(each %ifaces) {
        my @prefix = $_ =~ /(^[a-z]+)/;
        if(exists $pseudo{$prefix[0]}) {
            delete $ifaces{$_};
        }
    }

    return %ifaces;
}

sub ifstated {
    my ($stop) = @_;
    state $pid = -1;

    if(defined $stop) {
        if($pid >= 1 && waitpid($pid, WNOHANG) == 0) {
            kill 'TERM', $pid;
        }
        return;
    }

    my %ifaces = list_ifaces();
    my $fh = File::Temp->new();
    my @state_lines = ();
    foreach my $iface(keys %ifaces) {
        print $fh "${iface} = \"$iface.link.up\"\n";
        push @state_lines, "if \$$iface";
        push @state_lines, "run \"/usr/libexec/loghwevent up $iface\"";
        push @state_lines, "if ! \$$iface";
        push @state_lines, "run \"/usr/libexec/loghwevent down $iface\"";
    }
    print $fh "state initial {\n";
    print $fh join("\n", @state_lines);
    print $fh "\n}\n";
    $fh->flush();

    if($pid >= 0) {
        kill 'TERM', $pid;
        waitpid($pid, 0);
    }

    $pid = open3(my $fhin, my $fhout, my $fherr, '/usr/sbin/ifstated', '-d', '-f', $fh->filename) or die "$!\n";
    while(my $row = <$fhout>) {
        chomp $row;
        if($row eq 'started') {
            return;
        }
    }

    kill 'TERM', $pid;
    my $status = waitpid($pid, 0);
    if($status != 0) {
        warn "Failed to start ifstated\n";
        $pid = -1;
    }

    return;
}

sub ingest_hwevents {
    my ($fh) = @_;
    my @statinfo = stat $fh;
    if($fh->tell > $statinfo[7]) {
        $fh->seek(0, SEEK_SET);
    }

    while(my $row = <$fh>) {
        chomp $row;

        # Network interface was added or removed
        if($row =~ /^(attach|detach) 3 /) {
            ifstated();
        }
    }

    return;
}

sub autoconfigure {
    my ($interface) = @_;
    my $fh = IO::File->new("/etc/hostname.$interface",
                           O_CREAT|O_WRONLY|O_EXCL, '0640')
        or return;
    print $fh "dhcp\n";
    return;
}

sub handle_list {
    my %ifaces = list_ifaces();
    return 'ok ' . encode_json \%ifaces;
}

sub handle_connect {
    my ($interface) = @_;
    $interface = check_iface($interface);

    my %ifaces = list_ifaces();
    if(!exists $ifaces{$interface}) {
        return 'error NoSuchInterface';
    }

    # If there is no hostname.interface, set it up to use dhcp.
    autoconfigure($interface);

    run("/bin/sh /etc/netstart '$interface'");
    return 'ok';
}

sub handle_disconnect {
    my ($interface) = @_;
    $interface = check_iface($interface);

    my %ifaces = list_ifaces();
    if(!exists $ifaces{$interface}) {
        return 'error NoSuchInterface';
    }
    run("/sbin/ifconfig '$interface' down");
    return 'ok';
}

sub handle_configure {
    my ($options_text) = @_;
    my %options = %{decode_json $options_text};
    my ($iface) = ($options{'iface'} || '') =~ /^([a-z]+[0-9]*)$/;
    my ($nwid) = ($options{'nwid'} || '') =~ /^([\s ]+)$/;
    my ($wpakey) = ($options{'wpakey'} || '') =~ /^([\s ]+)$/;
    my ($inet) = ($options{'inet'} || '') =~ /^([0-9\.]+ [0-9\.]+ [0-9\.]+)$/;
    my ($inet6) = ($options{'inet6'} || '') =~ /^([0-9\.]+ [0-9\.]+ [0-9\.]+)$/;
    my ($dhcp) = ($options{'dhcp'} || '') eq JSON::PP::true;
    my ($rtsol) = ($options{'rtsol'} || '') eq JSON::PP::true;

    if(!$iface) {
        return 'error BadValue';
    }

    my $fh = IO::File->new("/etc/hostname.$iface",
                           O_CREAT|O_WRONLY, '0640') or return;

    if($dhcp) { print $fh "dhcp\n"; }
    if(defined $inet) { print $fh "inet $inet\n"; }
    if($rtsol) { print $fh "rtsol\n"; }
    if(defined $inet6) { print $fh "inet6 $inet6\n"; }

    if($nwid) { print $fh "nwid $nwid\n"; }
    if($wpakey) { print $fh "wpakey $wpakey\n"; }

    return;
}

my %DISPATCH = ();
$DISPATCH{'list'} = \&handle_list;
$DISPATCH{'connect'} = \&handle_connect;
$DISPATCH{'disconnect'} = \&handle_disconnect;
$DISPATCH{'configure'} = \&handle_configure;

sub handle {
    my ($sock) = @_;
    my $data = <$sock>;
    if(!defined $data) {
        return;
    }

    chomp $data;

    my @parts = split(' ', $data, 2);
    if($#parts < 0) {
        die "Empty message\n";
    }
    my $handler = $DISPATCH{$parts[0]};
    if(!defined $handler) {
        die "Unknown method: $parts[0]\n";
    }

    my $result = $handler->(@parts[1..$#parts]);
    return $result;
}

sub main {
    unlink $SOCK_PATH;
    my $server = IO::Socket::UNIX->new(
        Type => SOCK_STREAM(),
        Local => $SOCK_PATH,
        Listen => 1,
    ) or die "Failed to open server: $!\n";
    chmod 0660, $SOCK_PATH or die "Failed to set socket permissions: $!\n";
    my $gid = getgrnam('network') or die "Failed to get network gid: $!\n";
    chown 0, $gid, $SOCK_PATH or die "Failed to set socket ownership: $!\n";

    my $hwevent = IO::File->new("/var/run/hwevents", O_RDONLY);
    if(!$hwevent) {
        die "Failed to open hardware event log\n";
    }

    ifstated();
    ingest_hwevents($hwevent);

    my $kqueue = IO::KQueue->new();
    $kqueue->EV_SET(fileno($server), EVFILT_READ, EV_ADD);
    $kqueue->EV_SET(fileno($hwevent), EVFILT_READ, EV_ADD);

    print "Listening\n";
    my %fdhandles = ();
    while(1) {
        my @events = $kqueue->kevent();

        foreach my $ev(@events) {
            my $fd = $ev->[KQ_IDENT];
            if($fd == fileno($server)) {
                my $conn = $server->accept;
                $kqueue->EV_SET(fileno($conn), EVFILT_READ, EV_ADD);
                $fdhandles{fileno($conn)} = $conn;
            } elsif($fd == fileno($hwevent)) {
                ingest_hwevents($hwevent);
            } else {
                my $fh = $fdhandles{$fd};
                my $result = eval { handle($fh) } or do {
                    warn "$@\n" if $@;
                    $kqueue->EV_SET(fileno($fh), EVFILT_READ, EV_DELETE);
                    delete $fdhandles{$fd};
                    $fh->close();
                    next;
                };
                $fh->write("$result\n");
            }
        }
    }

    return;
}

sub END { ifstated(1); return; }
sub cleanup { ifstated(1); return; }

local $SIG{PIPE} = 'IGNORE';
local $SIG{INT} = &cleanup;
local $SIG{TERM} = &cleanup;

main();
