#!/usr/bin/perl -T
use strict;
use warnings;

use Pod::Usage;
use Getopt::Long;
use Carp;
use Unix::Pledge;
use IO::Socket::UNIX;

local $ENV{'PATH'} = '/bin:/sbin:/usr/bin:/usr/sbin';
my $SOCK_PATH = "/var/run/network.sock";

sub send_message {
    my ($socket, $command) = @_;
    $socket->send("$command\n");
    my $response = <$socket>;
    if(!defined $response) { $response = ''; }
    chomp $response;

    my @parts = $response =~ /^(ok|error)(?: (.*))?/;
    if($#parts < 0) { die "Bad response\n"; }
    if($parts[0] ne 'ok') {
        if($#parts >= 1) {
            die "Failed: $parts[1]\n";
        }
        die "Failed\n";
    }

    return $parts[1];
}

sub prompt {
    my ($socket) = @_;

    while(1) {
        print "> ";
        my $command = <>;
        if(!defined $command) { last; }
        chomp $command;

        my $response = send_message($socket, $command);
        if(defined $response) {
            print "$response\n";
        }
    }

    return;
}

sub main {
    my $socket = IO::Socket::UNIX->new(
        Type => SOCK_STREAM(),
        Peer => $SOCK_PATH,
    ) or die "Failed to connect to networkd: $!\n";

    pledge('stdio unix rpath');

    my $help = 0;
    my $prompt = 0;
    GetOptions('prompt' => \$prompt, 'help|?' => \$help) or pod2usage(2);
    pod2usage(1) if $help;

    if($prompt) {
        prompt($socket);
        return;
    }

    if($#ARGV >= 0) {
        my $command = join(' ', @ARGV);
        my $response = send_message($socket, $command);
        print "$response\n" if defined $response;
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

network --prompt

=cut
