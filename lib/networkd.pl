#!/usr/bin/perl -T
use strict;
use warnings;

use Carp;
use Unix::Pledge;
use IO::Socket::UNIX;
use IO::Select;
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

sub handle_status {
    my @ifaces = list_ifaces();
}

my %DISPATCH = ();
$DISPATCH{'list'} = \&handle_list;
$DISPATCH{'connect'} = \&handle_connect;
$DISPATCH{'disconnect'} = \&handle_disconnect;

sub handle {
    my ($sock) = @_;
    my $data = <$sock>;
    if(!defined $data) {
        return;
    }

    chomp $data;

    my @parts = split(' ', $data);
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

    # This doesn't really buy us much, but we might as well.
    pledge('stdio unix proc exec rpath wpath cpath');

    my $select = IO::Select->new($server);
    print "Listening\n";

    while(my @ready = $select->can_read) {
        foreach my $fh (@ready) {
            if($fh == $server) {
                $select->add($server->accept);
            } else {
                my $result = eval { handle($fh) } or do {
                    warn "$@\n" if $@;
                    $select->remove($fh);
                    $fh->close();
                    next;
                };
                $fh->write("$result\n");
            }
        }
    }

    return;
}

local $SIG{PIPE} = 'IGNORE';
main();
