#!/usr/bin/perl -T
use strict;
use warnings;

use Pod::Usage;
use Getopt::Long;
use Carp;
use JSON::PP;
use IO::Socket::UNIX;

my $SOCK_PATH = "/var/run/networkd.sock";

sub send_message {
    my ($socket, @command) = @_;
    my $encoded = encode_json \@command;
    $socket->send("$encoded\n");
    my $raw_response = <$socket>;
    if(!defined $raw_response) { $raw_response = ''; }

    my @parts = @{decode_json $raw_response};
    if($#parts < 0) { die "Bad response\n"; }
    if($parts[0] ne 'ok') {
        if($#parts >= 1) {
            die "Failed: $parts[1]\n";
        }
        die "Failed\n";
    }

    return @parts[1..$#parts];
}

sub handle_list {
    my ($sock, @args) = @_;
    my %response = send_message($sock, ['list']);
    my @keys = sort(keys %response);
    foreach my $key(@keys) {
        my $value = $response{$key};
        printf("%-20s%s\n", $key, $value);
    }

    return;
}

sub handle_connect {
    my ($sock, @args) = @_;
    if($#args != 0) { pod2usage(1); }

    my @response = send_message($sock, ['connect', $args[0]]);
    return;

}

sub handle_disconnect {
    my ($sock, @args) = @_;
    if($#args != 0) { pod2usage(1); }

    my @response = send_message($sock, ['disconnect', $args[0]]);
    return;
}

sub handle_configure { return; }

my %DISPATCH = ();
$DISPATCH{'list'} = \&handle_list;
$DISPATCH{'connect'} = \&handle_connect;
$DISPATCH{'disconnect'} = \&handle_disconnect;
$DISPATCH{'configure'} = \&handle_configure;

sub main {
    my $socket = IO::Socket::UNIX->new(
        Type => SOCK_STREAM(),
        Peer => $SOCK_PATH,
    ) or die "Failed to connect to networkd: $!\n";

    my $help = 0;
    GetOptions('help|?' => \$help) or pod2usage(2);
    pod2usage(1) if $help;

    if($#ARGV >= 0) {
        my $handler = $DISPATCH{$ARGV[0]};
        if(!defined $handler) {
            die "Unknown method: $ARGV[0]\n";
        }

        $handler->($socket, @ARGV[1..$#ARGV]);

        return;
    }

    pod2usage(1) if not $help;
    return;
}

main();

__END__

=head1 NAME

network - Network management client

=head1 SYNOPSIS

network list

network (connect | disconnect) <interface>

network configure <interface> <stanza>...

network --prompt

=cut
