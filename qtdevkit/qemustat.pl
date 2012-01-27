#!/usr/bin/perl -w
#
# qemustat.pl -- create statistics of cputransp.log
#

use strict;

my (%paths, %progs, %pids);
my $nexec = 0;
my $total = 0;

$\ = "\n";
$, = ", ";
my $cap = qr/.*?/;
while (<>)
{
	if (/^\[$cap\s+(\d+)\]\s*
		method:\s*"$cap"\s*
		pwd:\s*"($cap)"\s*
		cmd:\s*"([^\s]+)/x)
	{
		my ($pid, $cwd, $cmd) = ($1, $2, $3);
		$paths{$cwd}->[0]++;
		$progs{$cmd}->[0]++;
		$pids{$pid} = [ $cwd, $cmd ];
		$nexec++;
	} elsif (/^\[$cap\s+(\d+)\]\s*
		(?:rc:\s*\d+|signal:\s*"[\w\s]+")\s*
		time:\s*(\d+(?:\.\d+)?)/x)
	{
		my $p;
		my ($pid, $elapsed) = ($1, $2);
		if (defined ($p = delete $pids{$pid}))
		{
			$paths{$$p[0]}->[1] += $elapsed;
			$progs{$$p[1]}->[1] += $elapsed;
		}
		$total += $elapsed;
	}
}

sub show
{
	my ($what, $trim) = @_;

	foreach (sort({ ($$b[2]||0) <=> ($$a[2]||0) }
			map({ [ $_, @{$$what{$_}} ] } keys(%$what))))
	{
		my ($prg, $nexec, $time) = @$_;

		$prg =~ s!^.*/+!! if $trim;
		print defined $time
			? sprintf('%-30s: nexec=%-5u time=%fs',	$prg, $nexec, $time)
			: sprintf('%-30s: nexec=%-5u',		$prg, $nexec);
	}
}

$progs{'TOTAL'} = [ $nexec, $total ];
show(\%progs, 1);
#print '';
#show(\%paths, 0);

# End of qemustat.pl
